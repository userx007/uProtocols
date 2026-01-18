// Using prost for Protocol Buffers in Rust
use prost::Message;

// Define optimized message structures
#[derive(Clone, PartialEq, Message)]
pub struct OptimizedUser {
    // Low field numbers (1-15) for frequently used fields
    #[prost(string, tag = "1")]
    pub biography: String,
    
    #[prost(int32, tag = "2")]
    pub small_counter: i32,
    
    #[prost(uint32, tag = "3")]
    pub user_id: u32,
    
    // Packed repeated field (default in proto3)
    #[prost(int32, repeated, packed = "true", tag = "4")]
    pub scores: Vec<i32>,
    
    #[prost(string, tag = "16")]
    pub nickname: String,
}

#[derive(Clone, PartialEq, Message)]
pub struct UnoptimizedUser {
    #[prost(string, tag = "16")]
    pub biography: String,
    
    #[prost(int64, tag = "17")]
    pub small_counter: i64,
    
    #[prost(fixed32, tag = "18")]
    pub user_id: u32,
    
    #[prost(int32, repeated, tag = "19")]
    pub scores: Vec<i32>,
    
    #[prost(string, tag = "1")]
    pub nickname: String,
}

#[derive(Clone, PartialEq, Message)]
pub struct MetricsData {
    #[prost(uint32, tag = "1")]
    pub count: u32,
    
    // sint32 uses zigzag encoding for negative numbers
    #[prost(sint32, tag = "2")]
    pub temperature: i32,
    
    #[prost(fixed64, tag = "3")]
    pub timestamp_nanos: u64,
    
    #[prost(bool, tag = "4")]
    pub is_active: bool,
    
    #[prost(float, tag = "5")]
    pub precision_value: f32,
}

#[derive(Clone, PartialEq, Message)]
pub struct Notification {
    #[prost(oneof = "NotificationPayload", tags = "1, 2, 3")]
    pub payload: Option<NotificationPayload>,
    
    #[prost(uint32, tag = "4")]
    pub timestamp: u32,
}

#[derive(Clone, PartialEq, prost::Oneof)]
pub enum NotificationPayload {
    #[prost(string, tag = "1")]
    TextMessage(String),
    
    #[prost(bytes, tag = "2")]
    ImageData(Vec<u8>),
    
    #[prost(string, tag = "3")]
    VideoUrl(String),
}

#[derive(Clone, PartialEq, Message)]
pub struct DataPoints {
    #[prost(int32, repeated, packed = "true", tag = "1")]
    pub measurements: Vec<i32>,
}

fn demonstrate_message_sizes() {
    println!("=== Protocol Buffer Message Size Optimization in Rust ===\n");
    
    // Example 1: Field numbering impact
    let unopt_user = UnoptimizedUser {
        biography: "Software engineer with 10 years experience".to_string(),
        small_counter: 42,
        user_id: 12345,
        nickname: "dev_user".to_string(),
        scores: vec![],
    };
    
    let opt_user = OptimizedUser {
        biography: "Software engineer with 10 years experience".to_string(),
        small_counter: 42,
        user_id: 12345,
        nickname: "dev_user".to_string(),
        scores: vec![],
    };
    
    println!("Unoptimized size: {} bytes", unopt_user.encoded_len());
    println!("Optimized size: {} bytes\n", opt_user.encoded_len());
    
    // Example 2: Packed repeated fields
    let packed_data = DataPoints {
        measurements: (0..100).collect(),
    };
    
    println!("100 integers (packed): {} bytes\n", packed_data.encoded_len());
    
    // Example 3: Type selection impact
    let metrics = MetricsData {
        count: 50,
        temperature: -25,
        timestamp_nanos: 1234567890123456789,
        is_active: true,
        precision_value: 3.14159,
    };
    
    println!("Metrics data size: {} bytes\n", metrics.encoded_len());
    
    // Example 4: Oneof efficiency
    let notif1 = Notification {
        payload: Some(NotificationPayload::TextMessage("Hello!".to_string())),
        timestamp: 1234567890,
    };
    
    let notif2 = Notification {
        payload: Some(NotificationPayload::VideoUrl(
            "https://example.com/video.mp4".to_string()
        )),
        timestamp: 1234567890,
    };
    
    println!("Notification (text): {} bytes", notif1.encoded_len());
    println!("Notification (video url): {} bytes\n", notif2.encoded_len());
    
    // Example 5: Optional field optimization
    let sparse_user = OptimizedUser {
        biography: String::new(),
        small_counter: 0,
        user_id: 999,
        nickname: String::new(),
        scores: vec![],
    };
    
    println!("Sparse user (1 field): {} bytes", sparse_user.encoded_len());
    
    let full_user = OptimizedUser {
        biography: "Full profile with all fields populated".to_string(),
        small_counter: 100,
        user_id: 999,
        nickname: "power_user".to_string(),
        scores: (90..100).collect(),
    };
    
    println!("Full user (all fields): {} bytes\n", full_user.encoded_len());
}

fn analyze_field_sizes() {
    println!("Field-by-field size analysis:");
    
    let mut user = OptimizedUser {
        biography: String::new(),
        small_counter: 0,
        user_id: 0,
        nickname: String::new(),
        scores: vec![],
    };
    
    println!("Empty message: {} bytes", user.encoded_len());
    
    user.biography = "Test bio".to_string();
    println!("After biography: {} bytes", user.encoded_len());
    
    user.small_counter = 42;
    println!("After counter: {} bytes", user.encoded_len());
    
    user.user_id = 12345;
    println!("After user_id: {} bytes", user.encoded_len());
    
    user.scores = vec![0, 10, 20, 30, 40];
    println!("After 5 scores: {} bytes\n", user.encoded_len());
}

fn compare_integer_types() {
    println!("Integer encoding efficiency:");
    
    let value = 1000000;
    
    let data1 = MetricsData {
        count: value,
        temperature: 0,
        timestamp_nanos: 0,
        is_active: false,
        precision_value: 0.0,
    };
    
    println!("uint32 (varint) for {}: {} bytes", value, data1.encoded_len());
    println!("fixed32 would be: 5 bytes (always)");
    
    let data_small = MetricsData {
        count: 10,
        temperature: 0,
        timestamp_nanos: 0,
        is_active: false,
        precision_value: 0.0,
    };
    
    println!("uint32 (varint) for 10: {} bytes", data_small.encoded_len());
    println!("fixed32 for 10 would be: 5 bytes (always)\n");
}

fn demonstrate_serialization() {
    println!("Serialization example:");
    
    let user = OptimizedUser {
        biography: "Rust developer".to_string(),
        small_counter: 10,
        user_id: 42,
        nickname: "rustacean".to_string(),
        scores: vec![95, 98, 92, 100],
    };
    
    // Encode to bytes
    let mut buf = Vec::new();
    user.encode(&mut buf).expect("Failed to encode");
    
    println!("Serialized size: {} bytes", buf.len());
    println!("Buffer content (first 20 bytes): {:?}", 
             &buf[..buf.len().min(20)]);
    
    // Decode from bytes
    let decoded = OptimizedUser::decode(&buf[..])
        .expect("Failed to decode");
    
    println!("Successfully decoded: user_id = {}", decoded.user_id);
    println!("Scores: {:?}\n", decoded.scores);
}

fn main() {
    demonstrate_message_sizes();
    analyze_field_sizes();
    compare_integer_types();
    demonstrate_serialization();
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_optimized_smaller_than_unoptimized() {
        let unopt = UnoptimizedUser {
            biography: "Test".to_string(),
            small_counter: 42,
            user_id: 12345,
            nickname: "user".to_string(),
            scores: vec![],
        };
        
        let opt = OptimizedUser {
            biography: "Test".to_string(),
            small_counter: 42,
            user_id: 12345,
            nickname: "user".to_string(),
            scores: vec![],
        };
        
        assert!(opt.encoded_len() <= unopt.encoded_len());
    }
    
    #[test]
    fn test_packed_efficiency() {
        let data = DataPoints {
            measurements: vec![1, 2, 3, 4, 5],
        };
        
        // Packed should be much smaller than unpacked
        // 5 values * 2 bytes each (unpacked) = 10+ bytes
        // Packed: tag + length + values = ~7 bytes
        assert!(data.encoded_len() < 10);
    }
    
    #[test]
    fn test_oneof_size() {
        let notif1 = Notification {
            payload: Some(NotificationPayload::TextMessage("Hi".to_string())),
            timestamp: 100,
        };
        
        let notif2 = Notification {
            payload: Some(NotificationPayload::VideoUrl("url".to_string())),
            timestamp: 100,
        };
        
        // Both should be relatively small and similar in size
        assert!(notif1.encoded_len() < 20);
        assert!(notif2.encoded_len() < 20);
    }
}