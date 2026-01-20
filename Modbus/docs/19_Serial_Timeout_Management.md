# Serial Timeout Management in Modbus

## Overview

Modbus RTU serial communication relies on precise timing to detect frame boundaries. Unlike Modbus TCP which uses explicit length fields, Modbus RTU uses silent periods (timeouts) to identify the start and end of message frames. Two critical timeouts govern this behavior:

- **t1.5 (Inter-character timeout)**: Maximum time allowed between characters within a frame
- **t3.5 (Inter-frame timeout)**: Minimum silent period required between consecutive frames

When these timeouts are violated, the receiver discards the incomplete frame and waits for the next valid message.

## Timing Calculations

The timeout values are based on the time required to transmit a specific number of character bits at the current baud rate.

### Character Transmission Time

For Modbus RTU, one character typically consists of:
- 1 start bit
- 8 data bits
- 1 parity bit (even parity standard)
- 1 stop bit
- **Total: 11 bits per character**

**Time per character** = (11 bits) / (baud rate)

### Timeout Formulas

- **t1.5** = 1.5 × character time
- **t3.5** = 3.5 × character time

### Special Case: High Baud Rates

At baud rates above 19,200 bps, the calculated timeouts become extremely small and difficult to implement accurately. The Modbus specification recommends using fixed minimum values:

- **t1.5** ≥ 750 μs
- **t3.5** ≥ 1750 μs

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdio.h>

// Modbus timeout configuration structure
typedef struct {
    uint32_t baud_rate;
    uint32_t t1_5_us;  // Inter-character timeout in microseconds
    uint32_t t3_5_us;  // Inter-frame timeout in microseconds
} modbus_timing_t;

// Calculate Modbus RTU timeouts
void modbus_calculate_timeouts(modbus_timing_t *timing) {
    const uint32_t BITS_PER_CHAR = 11;
    const uint32_t MIN_T1_5_US = 750;
    const uint32_t MIN_T3_5_US = 1750;
    
    if (timing->baud_rate > 19200) {
        // Use fixed timeouts for high baud rates
        timing->t1_5_us = MIN_T1_5_US;
        timing->t3_5_us = MIN_T3_5_US;
    } else {
        // Calculate based on character transmission time
        // char_time_us = (bits_per_char * 1,000,000) / baud_rate
        uint32_t char_time_us = (BITS_PER_CHAR * 1000000UL) / timing->baud_rate;
        
        timing->t1_5_us = (char_time_us * 3) / 2;  // 1.5 times
        timing->t3_5_us = (char_time_us * 7) / 2;  // 3.5 times
    }
}

// Timer-based frame detection example
typedef struct {
    uint8_t buffer[256];
    uint16_t pos;
    uint64_t last_char_time_us;
    modbus_timing_t timing;
} modbus_receiver_t;

// Simulated microsecond timer (platform-specific in real code)
uint64_t get_microseconds(void);

// Process incoming character
void modbus_receive_char(modbus_receiver_t *rx, uint8_t byte) {
    uint64_t now = get_microseconds();
    uint64_t elapsed = now - rx->last_char_time_us;
    
    // Check for inter-character timeout
    if (rx->pos > 0 && elapsed > rx->timing.t1_5_us) {
        // Timeout exceeded - discard incomplete frame
        printf("Inter-character timeout! Discarding %u bytes\n", rx->pos);
        rx->pos = 0;
    }
    
    // Check for frame start (inter-frame gap)
    if (rx->pos == 0 && elapsed > rx->timing.t3_5_us) {
        // Valid frame start after sufficient silence
    }
    
    // Store character
    if (rx->pos < sizeof(rx->buffer)) {
        rx->buffer[rx->pos++] = byte;
        rx->last_char_time_us = now;
    }
}

// Check if complete frame received (call periodically)
int modbus_frame_complete(modbus_receiver_t *rx) {
    if (rx->pos == 0) return 0;
    
    uint64_t elapsed = get_microseconds() - rx->last_char_time_us;
    
    // Frame complete after t3.5 silence
    if (elapsed >= rx->timing.t3_5_us) {
        return 1;
    }
    return 0;
}

// Example usage
int main(void) {
    modbus_timing_t timing = {.baud_rate = 9600};
    modbus_calculate_timeouts(&timing);
    
    printf("Baud Rate: %u\n", timing.baud_rate);
    printf("t1.5: %u μs\n", timing.t1_5_us);
    printf("t3.5: %u μs\n", timing.t3_5_us);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};

const BITS_PER_CHAR: u32 = 11;
const MIN_T1_5_US: u32 = 750;
const MIN_T3_5_US: u32 = 1750;

#[derive(Debug, Clone)]
pub struct ModbusTiming {
    pub baud_rate: u32,
    pub t1_5: Duration,
    pub t3_5: Duration,
}

impl ModbusTiming {
    pub fn new(baud_rate: u32) -> Self {
        let (t1_5, t3_5) = if baud_rate > 19200 {
            // Use fixed timeouts for high baud rates
            (
                Duration::from_micros(MIN_T1_5_US as u64),
                Duration::from_micros(MIN_T3_5_US as u64),
            )
        } else {
            // Calculate based on character transmission time
            let char_time_us = (BITS_PER_CHAR * 1_000_000) / baud_rate;
            (
                Duration::from_micros(((char_time_us * 3) / 2) as u64),
                Duration::from_micros(((char_time_us * 7) / 2) as u64),
            )
        };

        ModbusTiming {
            baud_rate,
            t1_5,
            t3_5,
        }
    }
}

pub struct ModbusReceiver {
    buffer: Vec<u8>,
    last_char_time: Option<Instant>,
    timing: ModbusTiming,
    max_buffer_size: usize,
}

impl ModbusReceiver {
    pub fn new(baud_rate: u32, max_buffer_size: usize) -> Self {
        ModbusReceiver {
            buffer: Vec::with_capacity(max_buffer_size),
            last_char_time: None,
            timing: ModbusTiming::new(baud_rate),
            max_buffer_size,
        }
    }

    pub fn receive_byte(&mut self, byte: u8) -> Result<(), &'static str> {
        let now = Instant::now();

        if let Some(last_time) = self.last_char_time {
            let elapsed = now.duration_since(last_time);

            // Check inter-character timeout
            if !self.buffer.is_empty() && elapsed > self.timing.t1_5 {
                eprintln!(
                    "Inter-character timeout! Discarding {} bytes",
                    self.buffer.len()
                );
                self.buffer.clear();
            }

            // Check inter-frame gap for new frame
            if self.buffer.is_empty() && elapsed > self.timing.t3_5 {
                // Valid frame start after sufficient silence
            }
        }

        // Store byte
        if self.buffer.len() < self.max_buffer_size {
            self.buffer.push(byte);
            self.last_char_time = Some(now);
            Ok(())
        } else {
            Err("Buffer overflow")
        }
    }

    pub fn is_frame_complete(&self) -> bool {
        if self.buffer.is_empty() {
            return false;
        }

        if let Some(last_time) = self.last_char_time {
            let elapsed = Instant::now().duration_since(last_time);
            elapsed >= self.timing.t3_5
        } else {
            false
        }
    }

    pub fn get_frame(&mut self) -> Option<Vec<u8>> {
        if self.is_frame_complete() {
            Some(std::mem::take(&mut self.buffer))
        } else {
            None
        }
    }

    pub fn reset(&mut self) {
        self.buffer.clear();
        self.last_char_time = None;
    }
}

fn main() {
    // Example: Calculate timeouts for different baud rates
    let baud_rates = [9600, 19200, 38400, 115200];

    for &baud in &baud_rates {
        let timing = ModbusTiming::new(baud);
        println!("Baud Rate: {}", baud);
        println!("  t1.5: {:?}", timing.t1_5);
        println!("  t3.5: {:?}", timing.t3_5);
    }

    // Example: Receiver usage
    let mut receiver = ModbusReceiver::new(9600, 256);
    
    // Simulate receiving bytes
    receiver.receive_byte(0x01).unwrap();
    receiver.receive_byte(0x03).unwrap();
    
    // Check for complete frame (would need actual timing in real code)
    if receiver.is_frame_complete() {
        if let Some(frame) = receiver.get_frame() {
            println!("Received frame: {:02X?}", frame);
        }
    }
}
```

## Summary

Serial timeout management is fundamental to Modbus RTU communication, enabling frame boundary detection without explicit delimiters. The **t1.5 timeout** validates that characters within a frame arrive continuously, while the **t3.5 timeout** ensures adequate silence between frames. Proper implementation requires:

1. **Accurate timing calculations** based on baud rate and character format
2. **High-resolution timers** to measure inter-character intervals (microsecond precision)
3. **Special handling for high baud rates** (≥19,200 bps) using fixed minimum timeouts
4. **Robust error handling** to discard malformed frames when timeouts are violated

Both implementations demonstrate calculating timeouts based on transmission characteristics and detecting frame boundaries through elapsed time monitoring. In production systems, platform-specific high-resolution timers (like hardware timers or RTOS facilities) replace the simplified timing examples shown here.