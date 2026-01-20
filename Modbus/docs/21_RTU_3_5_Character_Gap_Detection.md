# RTU 3.5 Character Gap Detection

## Overview

In Modbus RTU (Remote Terminal Unit) mode, data is transmitted as a continuous stream of binary characters without explicit start/stop delimiters. To distinguish where one frame ends and another begins, Modbus RTU uses a timing-based mechanism: **a silent interval of at least 3.5 character times** marks the boundary between frames.

This timing requirement is critical for proper frame synchronization. If a device doesn't correctly detect these gaps, it may interpret multiple messages as one, corrupt data, or fail to respond to valid requests.

## The 3.5 Character Time Standard

A "character time" is the duration needed to transmit one complete character (typically 11 bits in Modbus: 1 start bit, 8 data bits, 1 parity bit, 1 stop bit). The 3.5 character gap provides a reliable silence period that won't occur within valid frame data.

**Calculation:**
```
Character_Time = 11 bits / Baud_Rate
Gap_Time = 3.5 × Character_Time
```

For example:
- At 9600 baud: Gap = 3.5 × (11/9600) ≈ 4.01 ms
- At 19200 baud: Gap = 3.5 × (11/19200) ≈ 2.00 ms
- At 115200 baud: Gap ≈ 0.33 ms (special handling needed)

At higher baud rates (>19200), the Modbus specification recommends using a fixed 1.75ms gap because hardware timer resolution becomes insufficient for accurate sub-millisecond timing.

## Implementation Challenges

1. **Timer Resolution**: System timers must have sufficient resolution to detect the gap accurately
2. **Interrupt Latency**: Delays in processing received characters can cause false gap detections
3. **High Baud Rates**: At speeds above 19200 baud, precise timing becomes difficult
4. **Partial Frames**: Handling incomplete or corrupted frames gracefully

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define MAX_FRAME_SIZE 256
#define MIN_BAUD_RATE 1200
#define MAX_BAUD_RATE 115200
#define HIGH_SPEED_THRESHOLD 19200
#define FIXED_GAP_MS 1.75

typedef struct {
    uint8_t buffer[MAX_FRAME_SIZE];
    size_t length;
    uint32_t baud_rate;
    uint32_t gap_us;  // Gap time in microseconds
    uint64_t last_char_time_us;
    bool frame_complete;
} ModbusRTUFrame;

// Calculate the required gap time in microseconds
uint32_t calculate_gap_time(uint32_t baud_rate) {
    if (baud_rate > HIGH_SPEED_THRESHOLD) {
        // Use fixed 1.75ms for high-speed communication
        return (uint32_t)(FIXED_GAP_MS * 1000);
    }
    
    // Character time = 11 bits / baud_rate (in seconds)
    // Gap time = 3.5 * character_time (in microseconds)
    double char_time_us = (11.0 / baud_rate) * 1000000.0;
    return (uint32_t)(3.5 * char_time_us);
}

// Get current time in microseconds
uint64_t get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Initialize RTU frame handler
void modbus_rtu_init(ModbusRTUFrame *frame, uint32_t baud_rate) {
    memset(frame->buffer, 0, MAX_FRAME_SIZE);
    frame->length = 0;
    frame->baud_rate = baud_rate;
    frame->gap_us = calculate_gap_time(baud_rate);
    frame->last_char_time_us = 0;
    frame->frame_complete = false;
}

// Process incoming character
bool modbus_rtu_process_char(ModbusRTUFrame *frame, uint8_t byte) {
    uint64_t current_time = get_time_us();
    
    // Check if gap has elapsed since last character
    if (frame->last_char_time_us > 0) {
        uint64_t elapsed = current_time - frame->last_char_time_us;
        
        if (elapsed >= frame->gap_us) {
            // Gap detected - previous frame is complete
            if (frame->length > 0) {
                frame->frame_complete = true;
                return true;  // Signal caller to process frame
            }
            // Start new frame
            frame->length = 0;
        }
    }
    
    // Add character to buffer
    if (frame->length < MAX_FRAME_SIZE) {
        frame->buffer[frame->length++] = byte;
        frame->last_char_time_us = current_time;
        frame->frame_complete = false;
    }
    
    return false;
}

// Check if frame is complete (call periodically)
bool modbus_rtu_check_timeout(ModbusRTUFrame *frame) {
    if (frame->length == 0) {
        return false;
    }
    
    uint64_t current_time = get_time_us();
    uint64_t elapsed = current_time - frame->last_char_time_us;
    
    if (elapsed >= frame->gap_us) {
        frame->frame_complete = true;
        return true;
    }
    
    return false;
}

// Reset frame for next reception
void modbus_rtu_reset(ModbusRTUFrame *frame) {
    frame->length = 0;
    frame->frame_complete = false;
    frame->last_char_time_us = 0;
}
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};

const MAX_FRAME_SIZE: usize = 256;
const HIGH_SPEED_THRESHOLD: u32 = 19200;
const FIXED_GAP_MS: f64 = 1.75;

pub struct ModbusRtuFrame {
    buffer: Vec<u8>,
    baud_rate: u32,
    gap_duration: Duration,
    last_char_time: Option<Instant>,
    frame_complete: bool,
}

impl ModbusRtuFrame {
    /// Create a new RTU frame handler with specified baud rate
    pub fn new(baud_rate: u32) -> Self {
        let gap_duration = Self::calculate_gap_time(baud_rate);
        
        Self {
            buffer: Vec::with_capacity(MAX_FRAME_SIZE),
            baud_rate,
            gap_duration,
            last_char_time: None,
            frame_complete: false,
        }
    }
    
    /// Calculate the required gap time based on baud rate
    fn calculate_gap_time(baud_rate: u32) -> Duration {
        if baud_rate > HIGH_SPEED_THRESHOLD {
            // Use fixed 1.75ms for high-speed communication
            Duration::from_micros((FIXED_GAP_MS * 1000.0) as u64)
        } else {
            // Character time = 11 bits / baud_rate
            // Gap time = 3.5 * character_time
            let char_time_us = (11.0 / baud_rate as f64) * 1_000_000.0;
            let gap_us = (3.5 * char_time_us) as u64;
            Duration::from_micros(gap_us)
        }
    }
    
    /// Process an incoming character
    /// Returns true if a complete frame was detected
    pub fn process_char(&mut self, byte: u8) -> bool {
        let current_time = Instant::now();
        let mut frame_ready = false;
        
        // Check if gap has elapsed since last character
        if let Some(last_time) = self.last_char_time {
            let elapsed = current_time.duration_since(last_time);
            
            if elapsed >= self.gap_duration {
                // Gap detected - previous frame is complete
                if !self.buffer.is_empty() {
                    self.frame_complete = true;
                    frame_ready = true;
                }
                // Start new frame
                self.buffer.clear();
            }
        }
        
        // Add character to buffer
        if self.buffer.len() < MAX_FRAME_SIZE {
            self.buffer.push(byte);
            self.last_char_time = Some(current_time);
            if !frame_ready {
                self.frame_complete = false;
            }
        }
        
        frame_ready
    }
    
    /// Check if timeout has occurred (call periodically)
    pub fn check_timeout(&mut self) -> bool {
        if self.buffer.is_empty() {
            return false;
        }
        
        if let Some(last_time) = self.last_char_time {
            let elapsed = Instant::now().duration_since(last_time);
            
            if elapsed >= self.gap_duration {
                self.frame_complete = true;
                return true;
            }
        }
        
        false
    }
    
    /// Get the current frame data if complete
    pub fn get_frame(&self) -> Option<&[u8]> {
        if self.frame_complete {
            Some(&self.buffer)
        } else {
            None
        }
    }
    
    /// Reset for next frame
    pub fn reset(&mut self) {
        self.buffer.clear();
        self.frame_complete = false;
        self.last_char_time = None;
    }
    
    /// Get gap duration for this baud rate
    pub fn gap_duration(&self) -> Duration {
        self.gap_duration
    }
}

// Example usage with async handling
#[cfg(feature = "tokio")]
use tokio::time::sleep;

#[cfg(feature = "tokio")]
pub async fn monitor_frame_timeout(frame: &mut ModbusRtuFrame) {
    loop {
        sleep(frame.gap_duration() / 4).await;  // Check at 1/4 gap interval
        
        if frame.check_timeout() {
            if let Some(data) = frame.get_frame() {
                println!("Complete frame received: {} bytes", data.len());
                // Process frame here
                frame.reset();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread::sleep;
    
    #[test]
    fn test_gap_calculation() {
        let frame_9600 = ModbusRtuFrame::new(9600);
        let expected_us = (3.5 * 11.0 / 9600.0 * 1_000_000.0) as u64;
        assert_eq!(frame_9600.gap_duration().as_micros() as u64, expected_us);
        
        let frame_115200 = ModbusRtuFrame::new(115200);
        assert_eq!(frame_115200.gap_duration().as_micros(), 1750);
    }
    
    #[test]
    fn test_frame_detection() {
        let mut frame = ModbusRtuFrame::new(9600);
        
        // Send some characters
        frame.process_char(0x01);
        frame.process_char(0x03);
        frame.process_char(0x00);
        
        // Wait for gap
        sleep(frame.gap_duration() + Duration::from_millis(1));
        
        // Next character should trigger frame completion
        assert!(frame.process_char(0x01));
    }
}
```

## Summary

**RTU 3.5 Character Gap Detection** is the fundamental mechanism for frame boundary detection in Modbus RTU communication. It relies on precise timing measurements to identify silent periods between frames, with the gap duration calculated as 3.5 times the transmission time of a single character. Key implementation considerations include accurate timer resolution, handling high-speed communications with fixed gap times, managing interrupt latency, and robust error handling for partial frames. Both C/C++ and Rust implementations demonstrate character-by-character processing with timestamp tracking, periodic timeout checking, and proper frame boundary detection. This timing-based approach enables reliable frame synchronization in the absence of explicit delimiters, making it essential for any Modbus RTU implementation.