# Bus Monitor Implementation

## Detailed Description

A **Profibus Bus Monitor** is a passive diagnostic tool that observes and records all communication traffic on a Profibus network without actively participating in the communication. Unlike active devices (masters or slaves), a bus monitor operates in receive-only mode, capturing telegrams for analysis, debugging, protocol verification, and network troubleshooting.

### Key Characteristics

**Passive Operation:**
- The bus monitor does not acknowledge telegrams or send data
- It has no station address and doesn't participate in the token ring
- Operates in "listen-only" mode to avoid interfering with network operation

**Use Cases:**
- Protocol analysis and debugging
- Network performance monitoring
- Commissioning and validation
- Troubleshooting intermittent errors
- Training and education
- Compliance testing

### Architecture Components

1. **Physical Layer Interface:** RS-485 transceiver configured for receive-only operation
2. **Telegram Capture Engine:** Hardware/software to capture raw bit streams
3. **Protocol Decoder:** Parses Profibus DP/FMS telegram structures
4. **Timestamp Module:** High-resolution timing for latency analysis
5. **Storage Buffer:** Circular buffer for captured telegrams
6. **Filter Engine:** Selective capture based on criteria
7. **Analysis Tools:** Statistics, error detection, visualization

### Telegram Structure to Monitor

```
Start Delimiter (SD) | Destination Address (DA) | Source Address (SA) | 
Function Code (FC) | Data Unit | Frame Check Sequence (FCS) | End Delimiter (ED)
```

### Implementation Considerations

- **Baud Rate Detection:** Auto-detect or configure (9.6 kbps to 12 Mbps)
- **Buffer Management:** Prevent overflow during high traffic
- **Timing Accuracy:** Microsecond-level timestamps
- **Error Detection:** Capture malformed frames and bus errors
- **Performance:** Real-time capture without packet loss

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

// Profibus telegram types
#define SD1 0x10  // Start delimiter without data field
#define SD2 0x68  // Start delimiter with data field
#define SD3 0xA2  // Start delimiter for fixed length frame
#define SD4 0xDC  // Token telegram
#define SC  0xE5  // Short confirmation
#define ED  0x16  // End delimiter

// Maximum frame size
#define MAX_FRAME_SIZE 256
#define BUFFER_SIZE 10000

// Frame structure
typedef struct {
    uint8_t start_delimiter;
    uint8_t length;
    uint8_t length_repeat;
    uint8_t destination_addr;
    uint8_t source_addr;
    uint8_t function_code;
    uint8_t data[MAX_FRAME_SIZE];
    uint16_t data_length;
    uint8_t fcs;  // Frame Check Sequence
    uint8_t end_delimiter;
    struct timespec timestamp;
    bool is_valid;
    char error_msg[64];
} profibus_frame_t;

// Circular buffer for captured frames
typedef struct {
    profibus_frame_t frames[BUFFER_SIZE];
    uint32_t write_idx;
    uint32_t read_idx;
    uint32_t count;
    uint32_t dropped;
} frame_buffer_t;

// Statistics structure
typedef struct {
    uint64_t total_frames;
    uint64_t valid_frames;
    uint64_t error_frames;
    uint64_t token_frames;
    uint64_t data_frames;
    uint64_t ack_frames;
    double avg_frame_time_us;
    uint32_t min_frame_size;
    uint32_t max_frame_size;
} monitor_stats_t;

// Bus monitor context
typedef struct {
    frame_buffer_t buffer;
    monitor_stats_t stats;
    bool running;
    uint32_t baud_rate;
    // Filter settings
    bool filter_enabled;
    uint8_t filter_source;
    uint8_t filter_destination;
} bus_monitor_t;

// Initialize bus monitor
void bus_monitor_init(bus_monitor_t *monitor, uint32_t baud_rate) {
    memset(monitor, 0, sizeof(bus_monitor_t));
    monitor->baud_rate = baud_rate;
    monitor->stats.min_frame_size = UINT32_MAX;
    monitor->running = true;
    printf("Bus Monitor initialized at %u baud\n", baud_rate);
}

// Calculate FCS (Frame Check Sequence)
uint8_t calculate_fcs(const uint8_t *data, uint16_t length) {
    uint8_t fcs = 0;
    for (uint16_t i = 0; i < length; i++) {
        fcs += data[i];
    }
    return fcs;
}

// Validate frame integrity
bool validate_frame(profibus_frame_t *frame) {
    // Check start delimiter
    if (frame->start_delimiter != SD1 && 
        frame->start_delimiter != SD2 &&
        frame->start_delimiter != SD3 && 
        frame->start_delimiter != SD4 &&
        frame->start_delimiter != SC) {
        snprintf(frame->error_msg, sizeof(frame->error_msg), 
                 "Invalid start delimiter: 0x%02X", frame->start_delimiter);
        return false;
    }

    // For SD2 frames, validate length fields
    if (frame->start_delimiter == SD2) {
        if (frame->length != frame->length_repeat) {
            snprintf(frame->error_msg, sizeof(frame->error_msg), 
                     "Length mismatch: %u != %u", frame->length, frame->length_repeat);
            return false;
        }
        
        // Calculate expected FCS
        uint8_t calc_buffer[MAX_FRAME_SIZE];
        uint16_t idx = 0;
        calc_buffer[idx++] = frame->destination_addr;
        calc_buffer[idx++] = frame->source_addr;
        calc_buffer[idx++] = frame->function_code;
        for (uint16_t i = 0; i < frame->data_length; i++) {
            calc_buffer[idx++] = frame->data[i];
        }
        
        uint8_t expected_fcs = calculate_fcs(calc_buffer, idx);
        if (frame->fcs != expected_fcs) {
            snprintf(frame->error_msg, sizeof(frame->error_msg), 
                     "FCS error: expected 0x%02X, got 0x%02X", expected_fcs, frame->fcs);
            return false;
        }
    }

    // Check end delimiter
    if (frame->start_delimiter == SD2 && frame->end_delimiter != ED) {
        snprintf(frame->error_msg, sizeof(frame->error_msg), 
                 "Invalid end delimiter: 0x%02X", frame->end_delimiter);
        return false;
    }

    return true;
}

// Parse received telegram
bool parse_telegram(const uint8_t *raw_data, uint16_t raw_length, 
                    profibus_frame_t *frame) {
    if (raw_length < 1) return false;
    
    memset(frame, 0, sizeof(profibus_frame_t));
    clock_gettime(CLOCK_MONOTONIC, &frame->timestamp);
    
    uint16_t idx = 0;
    frame->start_delimiter = raw_data[idx++];
    
    switch (frame->start_delimiter) {
        case SD1:  // Fixed length frame without data
            if (raw_length < 6) return false;
            frame->destination_addr = raw_data[idx++];
            frame->source_addr = raw_data[idx++];
            frame->function_code = raw_data[idx++];
            frame->fcs = raw_data[idx++];
            frame->end_delimiter = raw_data[idx++];
            break;
            
        case SD2:  // Variable length frame
            if (raw_length < 4) return false;
            frame->length = raw_data[idx++];
            frame->length_repeat = raw_data[idx++];
            frame->start_delimiter = raw_data[idx++];  // SD2 repeated
            frame->destination_addr = raw_data[idx++];
            frame->source_addr = raw_data[idx++];
            frame->function_code = raw_data[idx++];
            
            // Extract data
            frame->data_length = frame->length - 3;  // Length includes DA, SA, FC
            if (frame->data_length > 0 && idx + frame->data_length < raw_length) {
                memcpy(frame->data, &raw_data[idx], frame->data_length);
                idx += frame->data_length;
            }
            
            frame->fcs = raw_data[idx++];
            frame->end_delimiter = raw_data[idx++];
            break;
            
        case SD3:  // Fixed length frame with data
            if (raw_length < 14) return false;
            frame->destination_addr = raw_data[idx++];
            frame->source_addr = raw_data[idx++];
            frame->function_code = raw_data[idx++];
            frame->data_length = 8;
            memcpy(frame->data, &raw_data[idx], 8);
            idx += 8;
            frame->fcs = raw_data[idx++];
            frame->end_delimiter = raw_data[idx++];
            break;
            
        case SD4:  // Token frame
            if (raw_length < 3) return false;
            frame->destination_addr = raw_data[idx++];
            frame->source_addr = raw_data[idx++];
            break;
            
        case SC:  // Short acknowledgment
            // No additional data
            break;
            
        default:
            return false;
    }
    
    frame->is_valid = validate_frame(frame);
    return true;
}

// Add frame to circular buffer
bool buffer_add_frame(frame_buffer_t *buffer, const profibus_frame_t *frame) {
    if (buffer->count >= BUFFER_SIZE) {
        buffer->dropped++;
        return false;
    }
    
    memcpy(&buffer->frames[buffer->write_idx], frame, sizeof(profibus_frame_t));
    buffer->write_idx = (buffer->write_idx + 1) % BUFFER_SIZE;
    buffer->count++;
    return true;
}

// Process captured frame
void process_frame(bus_monitor_t *monitor, const profibus_frame_t *frame) {
    // Apply filters
    if (monitor->filter_enabled) {
        if (monitor->filter_source != 0xFF && 
            frame->source_addr != monitor->filter_source) {
            return;
        }
        if (monitor->filter_destination != 0xFF && 
            frame->destination_addr != monitor->filter_destination) {
            return;
        }
    }
    
    // Update statistics
    monitor->stats.total_frames++;
    if (frame->is_valid) {
        monitor->stats.valid_frames++;
    } else {
        monitor->stats.error_frames++;
    }
    
    // Classify frame type
    if (frame->start_delimiter == SD4) {
        monitor->stats.token_frames++;
    } else if (frame->start_delimiter == SC) {
        monitor->stats.ack_frames++;
    } else {
        monitor->stats.data_frames++;
    }
    
    // Track frame sizes
    uint32_t frame_size = frame->data_length + 10;  // Approximate
    if (frame_size < monitor->stats.min_frame_size) {
        monitor->stats.min_frame_size = frame_size;
    }
    if (frame_size > monitor->stats.max_frame_size) {
        monitor->stats.max_frame_size = frame_size;
    }
    
    // Store in buffer
    buffer_add_frame(&monitor->buffer, frame);
}

// Display frame details
void display_frame(const profibus_frame_t *frame) {
    printf("\n--- Frame Capture ---\n");
    printf("Timestamp: %ld.%09ld\n", frame->timestamp.tv_sec, frame->timestamp.tv_nsec);
    printf("SD: 0x%02X ", frame->start_delimiter);
    
    switch (frame->start_delimiter) {
        case SD1: printf("(SD1)\n"); break;
        case SD2: printf("(SD2)\n"); break;
        case SD3: printf("(SD3)\n"); break;
        case SD4: printf("(Token)\n"); break;
        case SC:  printf("(Short ACK)\n"); break;
        default:  printf("(Unknown)\n"); break;
    }
    
    if (frame->start_delimiter != SC) {
        printf("DA: %u, SA: %u\n", frame->destination_addr, frame->source_addr);
        if (frame->start_delimiter != SD4) {
            printf("FC: 0x%02X\n", frame->function_code);
            if (frame->data_length > 0) {
                printf("Data (%u bytes): ", frame->data_length);
                for (uint16_t i = 0; i < frame->data_length && i < 16; i++) {
                    printf("%02X ", frame->data[i]);
                }
                if (frame->data_length > 16) printf("...");
                printf("\n");
            }
            printf("FCS: 0x%02X\n", frame->fcs);
        }
    }
    
    printf("Valid: %s", frame->is_valid ? "Yes" : "No");
    if (!frame->is_valid) {
        printf(" - Error: %s", frame->error_msg);
    }
    printf("\n");
}

// Print statistics
void print_statistics(const bus_monitor_t *monitor) {
    printf("\n=== Bus Monitor Statistics ===\n");
    printf("Total Frames:  %lu\n", monitor->stats.total_frames);
    printf("Valid Frames:  %lu (%.2f%%)\n", monitor->stats.valid_frames,
           100.0 * monitor->stats.valid_frames / (monitor->stats.total_frames + 1));
    printf("Error Frames:  %lu\n", monitor->stats.error_frames);
    printf("Token Frames:  %lu\n", monitor->stats.token_frames);
    printf("Data Frames:   %lu\n", monitor->stats.data_frames);
    printf("ACK Frames:    %lu\n", monitor->stats.ack_frames);
    printf("Dropped:       %u\n", monitor->buffer.dropped);
    printf("Frame Size:    %u - %u bytes\n", 
           monitor->stats.min_frame_size, monitor->stats.max_frame_size);
    printf("==============================\n");
}

// Simulated capture function (in real implementation, reads from hardware)
void simulate_capture(bus_monitor_t *monitor) {
    // Example: Token frame
    uint8_t token_frame[] = {SD4, 0x02, 0x01};
    profibus_frame_t frame;
    if (parse_telegram(token_frame, sizeof(token_frame), &frame)) {
        process_frame(monitor, &frame);
        display_frame(&frame);
    }
    
    // Example: Data exchange frame (SD2)
    uint8_t data_frame[] = {
        SD2, 0x08, 0x08, SD2,  // Start, length fields
        0x05, 0x02, 0x5D,      // DA=5, SA=2, FC=0x5D (Read request)
        0x01, 0x02, 0x03, 0x04, 0x05,  // 5 bytes data
        0x74,                  // FCS
        ED                     // End delimiter
    };
    if (parse_telegram(data_frame, sizeof(data_frame), &frame)) {
        process_frame(monitor, &frame);
        display_frame(&frame);
    }
}

int main() {
    bus_monitor_t monitor;
    bus_monitor_init(&monitor, 1500000);  // 1.5 Mbaud
    
    // Set filters (optional)
    monitor.filter_enabled = false;
    monitor.filter_source = 0xFF;
    monitor.filter_destination = 0xFF;
    
    printf("Starting bus monitoring...\n");
    
    // Simulate capturing some frames
    for (int i = 0; i < 5; i++) {
        simulate_capture(&monitor);
    }
    
    print_statistics(&monitor);
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use std::collections::VecDeque;

// Start delimiters
const SD1: u8 = 0x10;
const SD2: u8 = 0x68;
const SD3: u8 = 0xA2;
const SD4: u8 = 0xDC;
const SC: u8 = 0xE5;
const ED: u8 = 0x16;

const MAX_FRAME_SIZE: usize = 256;
const BUFFER_SIZE: usize = 10000;

#[derive(Debug, Clone, Copy, PartialEq)]
enum FrameType {
    FixedNoData,    // SD1
    Variable,       // SD2
    FixedWithData,  // SD3
    Token,          // SD4
    ShortAck,       // SC
}

#[derive(Debug, Clone)]
struct ProfibusFrame {
    start_delimiter: u8,
    frame_type: FrameType,
    destination_addr: u8,
    source_addr: u8,
    function_code: u8,
    data: Vec<u8>,
    fcs: u8,
    end_delimiter: u8,
    timestamp: Duration,
    is_valid: bool,
    error_msg: Option<String>,
}

impl ProfibusFrame {
    fn new() -> Self {
        ProfibusFrame {
            start_delimiter: 0,
            frame_type: FrameType::ShortAck,
            destination_addr: 0,
            source_addr: 0,
            function_code: 0,
            data: Vec::new(),
            fcs: 0,
            end_delimiter: 0,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap_or(Duration::from_secs(0)),
            is_valid: false,
            error_msg: None,
        }
    }
}

#[derive(Debug, Default)]
struct MonitorStats {
    total_frames: u64,
    valid_frames: u64,
    error_frames: u64,
    token_frames: u64,
    data_frames: u64,
    ack_frames: u64,
    min_frame_size: u32,
    max_frame_size: u32,
}

struct FrameBuffer {
    frames: VecDeque<ProfibusFrame>,
    max_size: usize,
    dropped: u32,
}

impl FrameBuffer {
    fn new(max_size: usize) -> Self {
        FrameBuffer {
            frames: VecDeque::with_capacity(max_size),
            max_size,
            dropped: 0,
        }
    }

    fn add_frame(&mut self, frame: ProfibusFrame) -> bool {
        if self.frames.len() >= self.max_size {
            self.dropped += 1;
            false
        } else {
            self.frames.push_back(frame);
            true
        }
    }

    fn get_frame(&mut self) -> Option<ProfibusFrame> {
        self.frames.pop_front()
    }
}

struct BusMonitor {
    buffer: FrameBuffer,
    stats: MonitorStats,
    running: bool,
    baud_rate: u32,
    filter_enabled: bool,
    filter_source: Option<u8>,
    filter_destination: Option<u8>,
}

impl BusMonitor {
    fn new(baud_rate: u32) -> Self {
        println!("Bus Monitor initialized at {} baud", baud_rate);
        BusMonitor {
            buffer: FrameBuffer::new(BUFFER_SIZE),
            stats: MonitorStats {
                min_frame_size: u32::MAX,
                ..Default::default()
            },
            running: true,
            baud_rate,
            filter_enabled: false,
            filter_source: None,
            filter_destination: None,
        }
    }

    fn calculate_fcs(data: &[u8]) -> u8 {
        data.iter().fold(0u8, |acc, &x| acc.wrapping_add(x))
    }

    fn validate_frame(frame: &mut ProfibusFrame) -> bool {
        // Check start delimiter
        match frame.start_delimiter {
            SD1 | SD2 | SD3 | SD4 | SC => {},
            _ => {
                frame.error_msg = Some(format!(
                    "Invalid start delimiter: 0x{:02X}", 
                    frame.start_delimiter
                ));
                return false;
            }
        }

        // Validate SD2 frames
        if frame.frame_type == FrameType::Variable {
            // Calculate expected FCS
            let mut calc_buffer = Vec::new();
            calc_buffer.push(frame.destination_addr);
            calc_buffer.push(frame.source_addr);
            calc_buffer.push(frame.function_code);
            calc_buffer.extend_from_slice(&frame.data);

            let expected_fcs = Self::calculate_fcs(&calc_buffer);
            if frame.fcs != expected_fcs {
                frame.error_msg = Some(format!(
                    "FCS error: expected 0x{:02X}, got 0x{:02X}",
                    expected_fcs, frame.fcs
                ));
                return false;
            }

            if frame.end_delimiter != ED {
                frame.error_msg = Some(format!(
                    "Invalid end delimiter: 0x{:02X}",
                    frame.end_delimiter
                ));
                return false;
            }
        }

        true
    }

    fn parse_telegram(&self, raw_data: &[u8]) -> Option<ProfibusFrame> {
        if raw_data.is_empty() {
            return None;
        }

        let mut frame = ProfibusFrame::new();
        let mut idx = 0;

        frame.start_delimiter = raw_data[idx];
        idx += 1;

        match frame.start_delimiter {
            SD1 => {
                if raw_data.len() < 6 {
                    return None;
                }
                frame.frame_type = FrameType::FixedNoData;
                frame.destination_addr = raw_data[idx]; idx += 1;
                frame.source_addr = raw_data[idx]; idx += 1;
                frame.function_code = raw_data[idx]; idx += 1;
                frame.fcs = raw_data[idx]; idx += 1;
                frame.end_delimiter = raw_data[idx];
            },
            SD2 => {
                if raw_data.len() < 4 {
                    return None;
                }
                frame.frame_type = FrameType::Variable;
                let length = raw_data[idx]; idx += 1;
                let length_repeat = raw_data[idx]; idx += 1;
                
                if length != length_repeat {
                    return None;
                }
                
                idx += 1; // Skip repeated SD2
                frame.destination_addr = raw_data[idx]; idx += 1;
                frame.source_addr = raw_data[idx]; idx += 1;
                frame.function_code = raw_data[idx]; idx += 1;

                let data_length = (length - 3) as usize;
                if data_length > 0 && idx + data_length < raw_data.len() {
                    frame.data = raw_data[idx..idx + data_length].to_vec();
                    idx += data_length;
                }

                frame.fcs = raw_data[idx]; idx += 1;
                frame.end_delimiter = raw_data[idx];
            },
            SD3 => {
                if raw_data.len() < 14 {
                    return None;
                }
                frame.frame_type = FrameType::FixedWithData;
                frame.destination_addr = raw_data[idx]; idx += 1;
                frame.source_addr = raw_data[idx]; idx += 1;
                frame.function_code = raw_data[idx]; idx += 1;
                frame.data = raw_data[idx..idx + 8].to_vec();
                idx += 8;
                frame.fcs = raw_data[idx]; idx += 1;
                frame.end_delimiter = raw_data[idx];
            },
            SD4 => {
                if raw_data.len() < 3 {
                    return None;
                }
                frame.frame_type = FrameType::Token;
                frame.destination_addr = raw_data[idx]; idx += 1;
                frame.source_addr = raw_data[idx];
            },
            SC => {
                frame.frame_type = FrameType::ShortAck;
            },
            _ => return None,
        }

        frame.is_valid = Self::validate_frame(&mut frame);
        Some(frame)
    }

    fn apply_filter(&self, frame: &ProfibusFrame) -> bool {
        if !self.filter_enabled {
            return true;
        }

        if let Some(src) = self.filter_source {
            if frame.source_addr != src {
                return false;
            }
        }

        if let Some(dst) = self.filter_destination {
            if frame.destination_addr != dst {
                return false;
            }
        }

        true
    }

    fn process_frame(&mut self, frame: ProfibusFrame) {
        if !self.apply_filter(&frame) {
            return;
        }

        // Update statistics
        self.stats.total_frames += 1;
        
        if frame.is_valid {
            self.stats.valid_frames += 1;
        } else {
            self.stats.error_frames += 1;
        }

        match frame.frame_type {
            FrameType::Token => self.stats.token_frames += 1,
            FrameType::ShortAck => self.stats.ack_frames += 1,
            _ => self.stats.data_frames += 1,
        }

        // Track frame sizes
        let frame_size = (frame.data.len() + 10) as u32;
        if frame_size < self.stats.min_frame_size {
            self.stats.min_frame_size = frame_size;
        }
        if frame_size > self.stats.max_frame_size {
            self.stats.max_frame_size = frame_size;
        }

        // Store in buffer
        self.buffer.add_frame(frame);
    }

    fn display_frame(frame: &ProfibusFrame) {
        println!("\n--- Frame Capture ---");
        println!("Timestamp: {:?}", frame.timestamp);
        print!("SD: 0x{:02X} ", frame.start_delimiter);
        
        match frame.frame_type {
            FrameType::FixedNoData => println!("(SD1)"),
            FrameType::Variable => println!("(SD2)"),
            FrameType::FixedWithData => println!("(SD3)"),
            FrameType::Token => println!("(Token)"),
            FrameType::ShortAck => println!("(Short ACK)"),
        }

        if frame.frame_type != FrameType::ShortAck {
            println!("DA: {}, SA: {}", frame.destination_addr, frame.source_addr);
            
            if frame.frame_type != FrameType::Token {
                println!("FC: 0x{:02X}", frame.function_code);
                
                if !frame.data.is_empty() {
                    print!("Data ({} bytes): ", frame.data.len());
                    for (i, byte) in frame.data.iter().take(16).enumerate() {
                        print!("{:02X} ", byte);
                    }
                    if frame.data.len() > 16 {
                        print!("...");
                    }
                    println!();
                }
                
                println!("FCS: 0x{:02X}", frame.fcs);
            }
        }

        print!("Valid: {}", if frame.is_valid { "Yes" } else { "No" });
        if let Some(ref err) = frame.error_msg {
            print!(" - Error: {}", err);
        }
        println!();
    }

    fn print_statistics(&self) {
        println!("\n=== Bus Monitor Statistics ===");
        println!("Total Frames:  {}", self.stats.total_frames);
        println!("Valid Frames:  {} ({:.2}%)", 
            self.stats.valid_frames,
            100.0 * self.stats.valid_frames as f64 / (self.stats.total_frames as f64 + 1.0)
        );
        println!("Error Frames:  {}", self.stats.error_frames);
        println!("Token Frames:  {}", self.stats.token_frames);
        println!("Data Frames:   {}", self.stats.data_frames);
        println!("ACK Frames:    {}", self.stats.ack_frames);
        println!("Dropped:       {}", self.buffer.dropped);
        println!("Frame Size:    {} - {} bytes", 
            self.stats.min_frame_size, self.stats.max_frame_size);
        println!("==============================");
    }

    fn simulate_capture(&mut self) {
        // Token frame
        let token_frame = vec![SD4, 0x02, 0x01];
        if let Some(frame) = self.parse_telegram(&token_frame) {
            Self::display_frame(&frame);
            self.process_frame(frame);
        }

        // Data exchange frame (SD2)
        let data_frame = vec![
            SD2, 0x08, 0x08, SD2,
            0x05, 0x02, 0x5D,
            0x01, 0x02, 0x03, 0x04, 0x05,
            0x74,
            ED
        ];
        if let Some(frame) = self.parse_telegram(&data_frame) {
            Self::display_frame(&frame);
            self.process_frame(frame);
        }
    }
}

fn main() {
    let mut monitor = BusMonitor::new(1_500_000); // 1.5 Mbaud
    
    println!("Starting bus monitoring...\n");
    
    // Simulate capturing frames
    for _ in 0..5 {
        monitor.simulate_capture();
    }
    
    monitor.print_statistics();
}
```

---

## Summary

**Bus Monitor Implementation** provides passive, non-intrusive observation of Profibus network traffic for debugging and analysis purposes. Key aspects include:

**Core Features:**
- **Passive reception** of all network telegrams without participation
- **Protocol decoding** of Profibus DP frame structures (SD1-SD4, SC)
- **Frame validation** with FCS checking and error detection
- **High-resolution timestamping** for latency analysis
- **Circular buffering** to handle high-speed traffic without loss

**Implementation Components:**
- Frame parsing for all telegram types (data, token, acknowledgment)
- Validation logic for frame integrity and protocol compliance
- Statistics collection (frame counts, errors, sizes)
- Optional filtering by source/destination address
- Buffer management to prevent overflow

**Practical Applications:**
- Network commissioning and validation
- Protocol conformance testing
- Performance monitoring and optimization
- Troubleshooting intermittent communication issues
- Training and educational demonstrations

Both C/C++ and Rust implementations demonstrate complete bus monitoring systems with frame capture, validation, buffering, and statistical analysis capabilities suitable for real-world Profibus diagnostic applications.