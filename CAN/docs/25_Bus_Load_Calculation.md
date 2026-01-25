# CAN Bus Load Calculation

## Overview

Bus load calculation is critical for ensuring CAN networks operate reliably without exceeding their capacity. When bus load becomes too high, messages experience increased latency, potential transmission failures, and reduced determinism. Proper bus load analysis helps prevent these issues during system design and operation.

## Understanding CAN Bus Load

### What is Bus Load?

Bus load represents the percentage of time the CAN bus is occupied transmitting data. It's calculated as:

```
Bus Load (%) = (Total Bits Transmitted / Total Available Bit Time) × 100
```

### Safe Operating Limits

- **Recommended maximum**: 30-40% for safety-critical systems
- **Acceptable range**: Up to 60-70% for non-critical applications
- **Critical threshold**: Above 80% risks message loss and unpredictable behavior

### Factors Affecting Bus Load

1. **Message frame overhead**: Start bit, arbitration, control, CRC, ACK, EOF, intermission
2. **Bit stuffing**: Every 5 consecutive identical bits require an opposite bit
3. **Error frames**: Retransmissions due to errors
4. **Bus speed**: Lower speeds = higher relative load for same data

## Calculating Message Frame Size

A CAN message consists of:

| Component | Standard CAN | Extended CAN |
|-----------|--------------|--------------|
| Start of Frame | 1 bit | 1 bit |
| Identifier | 11 bits | 29 bits |
| Control Field | 6 bits | 6 bits |
| Data Field | 0-64 bits | 0-64 bits |
| CRC Field | 16 bits | 16 bits |
| ACK Field | 2 bits | 2 bits |
| End of Frame | 7 bits | 7 bits |
| Intermission | 3 bits | 3 bits |
| **Base Total** | **46 + data** | **64 + data** |

### Bit Stuffing Overhead

Bit stuffing adds approximately 20% overhead on average, making the practical formula:

```
Frame bits ≈ (Base bits) × 1.2
```

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// CAN frame types
typedef enum {
    CAN_STANDARD = 0,
    CAN_EXTENDED = 1
} can_frame_type_t;

// Structure to represent a CAN message
typedef struct {
    uint32_t id;
    uint8_t dlc;  // Data length code (0-8)
    can_frame_type_t type;
    uint32_t period_ms;  // Transmission period in milliseconds
} can_message_t;

// Calculate bits for a single CAN frame
uint32_t calculate_frame_bits(const can_message_t* msg) {
    uint32_t base_bits;
    
    if (msg->type == CAN_STANDARD) {
        // Standard CAN: 1 + 11 + 6 + (DLC*8) + 16 + 2 + 7 + 3
        base_bits = 46 + (msg->dlc * 8);
    } else {
        // Extended CAN: 1 + 29 + 6 + (DLC*8) + 16 + 2 + 7 + 3
        base_bits = 64 + (msg->dlc * 8);
    }
    
    // Apply bit stuffing overhead (approximately 20%)
    return (uint32_t)(base_bits * 1.2);
}

// Calculate bus load percentage
float calculate_bus_load(const can_message_t* messages, 
                         uint32_t msg_count, 
                         uint32_t baudrate) {
    float total_bits_per_second = 0.0f;
    
    for (uint32_t i = 0; i < msg_count; i++) {
        uint32_t frame_bits = calculate_frame_bits(&messages[i]);
        
        // Calculate frequency (Hz)
        float frequency = 1000.0f / messages[i].period_ms;
        
        // Add to total bits per second
        total_bits_per_second += frame_bits * frequency;
    }
    
    // Calculate bus load percentage
    return (total_bits_per_second / baudrate) * 100.0f;
}

// Analyze individual message contributions
void analyze_message_load(const can_message_t* messages, 
                          uint32_t msg_count, 
                          uint32_t baudrate) {
    printf("Message Load Analysis:\n");
    printf("%-10s %-6s %-12s %-12s %-10s\n", 
           "ID", "DLC", "Period(ms)", "Bits/Frame", "Load(%)");
    printf("--------------------------------------------------------\n");
    
    float total_load = 0.0f;
    
    for (uint32_t i = 0; i < msg_count; i++) {
        uint32_t frame_bits = calculate_frame_bits(&messages[i]);
        float frequency = 1000.0f / messages[i].period_ms;
        float bits_per_sec = frame_bits * frequency;
        float msg_load = (bits_per_sec / baudrate) * 100.0f;
        
        printf("0x%-8X %-6d %-12d %-12d %-10.2f%%\n",
               messages[i].id,
               messages[i].dlc,
               messages[i].period_ms,
               frame_bits,
               msg_load);
        
        total_load += msg_load;
    }
    
    printf("--------------------------------------------------------\n");
    printf("Total Bus Load: %.2f%%\n", total_load);
    
    // Provide recommendations
    if (total_load < 30.0f) {
        printf("Status: Excellent - Low bus utilization\n");
    } else if (total_load < 60.0f) {
        printf("Status: Good - Acceptable utilization\n");
    } else if (total_load < 80.0f) {
        printf("Status: Warning - High utilization\n");
    } else {
        printf("Status: CRITICAL - Bus overload risk!\n");
    }
}

// Example usage
int main() {
    // Define CAN messages
    can_message_t messages[] = {
        {.id = 0x100, .dlc = 8, .type = CAN_STANDARD, .period_ms = 10},  // Engine data
        {.id = 0x200, .dlc = 8, .type = CAN_STANDARD, .period_ms = 20},  // Transmission
        {.id = 0x300, .dlc = 6, .type = CAN_STANDARD, .period_ms = 50},  // Sensors
        {.id = 0x400, .dlc = 4, .type = CAN_STANDARD, .period_ms = 100}, // Status
        {.id = 0x500, .dlc = 8, .type = CAN_STANDARD, .period_ms = 100}, // Diagnostics
    };
    
    uint32_t baudrate = 500000;  // 500 kbps
    uint32_t msg_count = sizeof(messages) / sizeof(messages[0]);
    
    printf("CAN Bus Load Calculator\n");
    printf("Baudrate: %d bps\n\n", baudrate);
    
    analyze_message_load(messages, msg_count, baudrate);
    
    float total_load = calculate_bus_load(messages, msg_count, baudrate);
    printf("\nCalculated Total Load: %.2f%%\n", total_load);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::fmt;

#[derive(Debug, Clone, Copy)]
enum CanFrameType {
    Standard,
    Extended,
}

#[derive(Debug, Clone)]
struct CanMessage {
    id: u32,
    dlc: u8,           // Data length code (0-8)
    frame_type: CanFrameType,
    period_ms: u32,    // Transmission period in milliseconds
}

impl CanMessage {
    /// Calculate the number of bits for this CAN frame including bit stuffing
    fn calculate_frame_bits(&self) -> u32 {
        let base_bits = match self.frame_type {
            // Standard: SOF(1) + ID(11) + CTRL(6) + DATA + CRC(16) + ACK(2) + EOF(7) + IFS(3)
            CanFrameType::Standard => 46 + (self.dlc as u32 * 8),
            // Extended: SOF(1) + ID(29) + CTRL(6) + DATA + CRC(16) + ACK(2) + EOF(7) + IFS(3)
            CanFrameType::Extended => 64 + (self.dlc as u32 * 8),
        };
        
        // Apply bit stuffing overhead (~20%)
        (base_bits as f32 * 1.2) as u32
    }
    
    /// Calculate this message's contribution to bus load
    fn calculate_load(&self, baudrate: u32) -> f32 {
        let frame_bits = self.calculate_frame_bits();
        let frequency = 1000.0 / self.period_ms as f32;
        let bits_per_second = frame_bits as f32 * frequency;
        
        (bits_per_second / baudrate as f32) * 100.0
    }
}

struct BusLoadAnalyzer {
    messages: Vec<CanMessage>,
    baudrate: u32,
}

impl BusLoadAnalyzer {
    fn new(baudrate: u32) -> Self {
        Self {
            messages: Vec::new(),
            baudrate,
        }
    }
    
    fn add_message(&mut self, message: CanMessage) {
        self.messages.push(message);
    }
    
    /// Calculate total bus load
    fn calculate_total_load(&self) -> f32 {
        self.messages.iter()
            .map(|msg| msg.calculate_load(self.baudrate))
            .sum()
    }
    
    /// Analyze and print detailed load information
    fn analyze(&self) {
        println!("CAN Bus Load Analysis");
        println!("Baudrate: {} bps\n", self.baudrate);
        println!("{:-<70}", "");
        println!("{:<10} {:<6} {:<12} {:<12} {:<10}", 
                 "ID", "DLC", "Period(ms)", "Bits/Frame", "Load(%)");
        println!("{:-<70}", "");
        
        let mut total_load = 0.0;
        
        for msg in &self.messages {
            let frame_bits = msg.calculate_frame_bits();
            let msg_load = msg.calculate_load(self.baudrate);
            
            println!("{:<10X} {:<6} {:<12} {:<12} {:<10.2}", 
                     msg.id, msg.dlc, msg.period_ms, frame_bits, msg_load);
            
            total_load += msg_load;
        }
        
        println!("{:-<70}", "");
        println!("Total Bus Load: {:.2}%\n", total_load);
        
        // Provide status assessment
        self.print_status(total_load);
    }
    
    fn print_status(&self, load: f32) {
        let status = if load < 30.0 {
            "Excellent - Low bus utilization"
        } else if load < 60.0 {
            "Good - Acceptable utilization"
        } else if load < 80.0 {
            "Warning - High utilization"
        } else {
            "CRITICAL - Bus overload risk!"
        };
        
        println!("Status: {}", status);
    }
    
    /// Find messages that can be optimized
    fn suggest_optimizations(&self) {
        println!("\nOptimization Suggestions:");
        
        let high_load_threshold = 5.0; // 5% individual load
        let high_load_msgs: Vec<_> = self.messages.iter()
            .filter(|msg| msg.calculate_load(self.baudrate) > high_load_threshold)
            .collect();
        
        if high_load_msgs.is_empty() {
            println!("No significant optimization opportunities found.");
        } else {
            for msg in high_load_msgs {
                let load = msg.calculate_load(self.baudrate);
                println!("- Message 0x{:X}: {:.2}% load", msg.id, load);
                
                if msg.period_ms < 50 {
                    println!("  Consider increasing period from {}ms", msg.period_ms);
                }
                if msg.dlc > 4 {
                    println!("  Consider reducing data payload if possible");
                }
            }
        }
    }
}

fn main() {
    let mut analyzer = BusLoadAnalyzer::new(500_000); // 500 kbps
    
    // Add CAN messages
    analyzer.add_message(CanMessage {
        id: 0x100,
        dlc: 8,
        frame_type: CanFrameType::Standard,
        period_ms: 10,
    });
    
    analyzer.add_message(CanMessage {
        id: 0x200,
        dlc: 8,
        frame_type: CanFrameType::Standard,
        period_ms: 20,
    });
    
    analyzer.add_message(CanMessage {
        id: 0x300,
        dlc: 6,
        frame_type: CanFrameType::Standard,
        period_ms: 50,
    });
    
    analyzer.add_message(CanMessage {
        id: 0x400,
        dlc: 4,
        frame_type: CanFrameType::Standard,
        period_ms: 100,
    });
    
    analyzer.add_message(CanMessage {
        id: 0x500,
        dlc: 8,
        frame_type: CanFrameType::Standard,
        period_ms: 100,
    });
    
    // Perform analysis
    analyzer.analyze();
    analyzer.suggest_optimizations();
}
```

## Optimization Strategies

### 1. **Reduce Transmission Frequency**
- Identify non-critical messages and increase their periods
- Use event-triggered transmission instead of periodic for infrequent data

### 2. **Optimize Data Packing**
- Combine related signals into fewer messages
- Reduce DLC when possible without losing information
- Use signal scaling to fit more data in fewer bytes

### 3. **Message Prioritization**
- Assign lower IDs (higher priority) to critical messages
- Ensure time-critical data has sufficient bandwidth

### 4. **Increase Baudrate**
- If hardware supports it, moving from 250 kbps to 500 kbps or 1 Mbps reduces load percentage

### 5. **Remove Redundant Messages**
- Audit the network for duplicate or unnecessary data
- Consolidate similar information sources

## Summary

**Bus load calculation** is essential for CAN network design and maintenance. The provided C/C++ and Rust implementations demonstrate how to:

- **Calculate frame sizes** including bit stuffing overhead
- **Compute bus load percentages** for individual messages and total network utilization
- **Analyze message contributions** to identify optimization opportunities
- **Provide status assessments** based on industry-standard thresholds

**Key takeaways**: Keep bus load under 30-40% for safety-critical systems, account for the 20% bit stuffing overhead in calculations, monitor both individual message loads and total utilization, and use analysis tools during design phase to prevent overload conditions. Proper bus load management ensures deterministic behavior, reduces latency, and improves overall CAN network reliability.