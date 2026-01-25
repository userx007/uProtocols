# CAN Bit Timing and Synchronization

## Detailed Description

Bit timing and synchronization are fundamental aspects of CAN (Controller Area Network) communication that ensure reliable data transmission between nodes on the bus. These mechanisms allow nodes with independent oscillators to maintain synchronized communication despite slight frequency variations.

### Core Concepts

**Bit Timing Structure**

Each CAN bit period is divided into four distinct segments:

1. **Synchronization Segment (Sync_Seg)**: Always 1 Time Quantum (TQ) long, where edges are expected to occur
2. **Propagation Segment (Prop_Seg)**: Compensates for physical delay times on the network (1-8 TQ)
3. **Phase Segment 1 (Phase_Seg1)**: Can be lengthened during resynchronization (1-8 TQ)
4. **Phase Segment 2 (Phase_Seg2)**: Can be shortened during resynchronization (1-8 TQ)

**Time Quantum (TQ)**

The Time Quantum is the smallest unit of time in CAN bit timing, calculated as:

```
TQ = (BRP + 1) / f_CAN_clock
```

where BRP is the Baud Rate Prescaler value.

**Sample Point**

The sample point is where the bus level is read and interpreted. It's located at the end of Phase_Seg1, typically positioned at 75-87.5% of the bit time for optimal noise immunity.

**Synchronization Jump Width (SJW)**

The SJW defines the maximum amount by which the bit timing can be lengthened or shortened during resynchronization (1-4 TQ). This allows nodes to compensate for clock drift and phase errors.

### Synchronization Types

1. **Hard Synchronization**: Occurs at the start of frame (SOF bit), forcing the bit timing to restart
2. **Resynchronization**: Occurs on recessive-to-dominant edges during the frame, adjusting phase segments by up to SJW

---

## C/C++ Code Examples

### Example 1: Basic Bit Timing Configuration (STM32 HAL)

```c
#include "stm32f4xx_hal.h"

// CAN bit timing configuration structure
typedef struct {
    uint32_t prescaler;      // Baud Rate Prescaler (1-1024)
    uint32_t time_seg1;      // Time Segment 1 (1-16)
    uint32_t time_seg2;      // Time Segment 2 (1-8)
    uint32_t sjw;            // Synchronization Jump Width (1-4)
    uint32_t sample_point;   // Calculated sample point percentage
} CAN_BitTimingConfig;

/**
 * Calculate bit timing parameters for desired baudrate
 * 
 * @param can_clock: CAN peripheral clock frequency in Hz
 * @param baudrate: Desired CAN baudrate in bps
 * @param config: Pointer to configuration structure
 * @return: 0 on success, -1 on error
 */
int calculate_bit_timing(uint32_t can_clock, uint32_t baudrate, 
                         CAN_BitTimingConfig *config) {
    // Target: 500 kbps, Sample Point at 87.5%
    // Common configurations for different clock speeds
    
    if (can_clock == 42000000 && baudrate == 500000) {
        // 42 MHz clock, 500 kbps
        // Bit time = 84 TQ (42 MHz / 500 kHz)
        config->prescaler = 6;        // BRP = 5 (value - 1)
        config->time_seg1 = 13;       // TSEG1 = 13 TQ
        config->time_seg2 = 2;        // TSEG2 = 2 TQ
        config->sjw = 1;              // SJW = 1 TQ
        
        // Total bit time = 1 (Sync) + 13 (TS1) + 2 (TS2) = 16 TQ
        // TQ = 6 / 42 MHz = 0.142857 µs
        // Bit time = 16 * 0.142857 µs = 2 µs (500 kbps)
        // Sample point = (1 + 13) / 16 = 87.5%
        
        config->sample_point = ((1 + config->time_seg1) * 100) / 
                               (1 + config->time_seg1 + config->time_seg2);
        return 0;
    }
    
    return -1; // Unsupported configuration
}

/**
 * Initialize CAN with calculated bit timing
 */
HAL_StatusTypeDef init_can_with_timing(CAN_HandleTypeDef *hcan) {
    CAN_BitTimingConfig timing;
    
    // Calculate timing for 42 MHz APB1 clock, 500 kbps
    if (calculate_bit_timing(42000000, 500000, &timing) != 0) {
        return HAL_ERROR;
    }
    
    hcan->Instance = CAN1;
    hcan->Init.Prescaler = timing.prescaler;
    hcan->Init.Mode = CAN_MODE_NORMAL;
    hcan->Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan->Init.TimeSeg1 = CAN_BS1_13TQ;
    hcan->Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan->Init.TimeTriggeredMode = DISABLE;
    hcan->Init.AutoBusOff = DISABLE;
    hcan->Init.AutoWakeUp = DISABLE;
    hcan->Init.AutoRetransmission = ENABLE;
    hcan->Init.ReceiveFifoLocked = DISABLE;
    hcan->Init.TransmitFifoPriority = DISABLE;
    
    return HAL_CAN_Init(hcan);
}
```

### Example 2: Advanced Bit Timing Calculator

```cpp
#include <iostream>
#include <vector>
#include <cmath>

class CANBitTimingCalculator {
private:
    struct TimingParams {
        uint32_t brp;
        uint32_t tseg1;
        uint32_t tseg2;
        uint32_t sjw;
        double sample_point;
        double actual_baudrate;
        double error_percent;
    };
    
    uint32_t can_clock_;
    
    // Constraints (ISO 11898-1)
    static constexpr uint32_t MIN_BRP = 1;
    static constexpr uint32_t MAX_BRP = 1024;
    static constexpr uint32_t MIN_TSEG1 = 1;
    static constexpr uint32_t MAX_TSEG1 = 16;
    static constexpr uint32_t MIN_TSEG2 = 1;
    static constexpr uint32_t MAX_TSEG2 = 8;
    static constexpr uint32_t MIN_SJW = 1;
    static constexpr uint32_t MAX_SJW = 4;
    
public:
    CANBitTimingCalculator(uint32_t can_clock) : can_clock_(can_clock) {}
    
    /**
     * Find optimal bit timing parameters
     * 
     * @param target_baudrate: Desired baudrate in bps
     * @param target_sample_point: Desired sample point (0.0-1.0)
     * @return: Optimal timing parameters
     */
    TimingParams calculate_optimal_timing(uint32_t target_baudrate, 
                                          double target_sample_point = 0.875) {
        std::vector<TimingParams> candidates;
        
        // Iterate through possible BRP values
        for (uint32_t brp = MIN_BRP; brp <= MAX_BRP; brp++) {
            double tq_freq = static_cast<double>(can_clock_) / brp;
            double ntq = tq_freq / target_baudrate; // Number of TQ per bit
            
            // Total TQ must be between 8 and 25 for valid timing
            if (ntq < 8.0 || ntq > 25.0) continue;
            
            uint32_t total_tq = static_cast<uint32_t>(std::round(ntq));
            
            // Calculate segments based on sample point
            // Sample point = (1 + TSEG1) / (1 + TSEG1 + TSEG2)
            uint32_t tseg1 = static_cast<uint32_t>(
                std::round((total_tq - 1) * target_sample_point) - 1
            );
            uint32_t tseg2 = total_tq - 1 - tseg1;
            
            // Validate segments
            if (tseg1 < MIN_TSEG1 || tseg1 > MAX_TSEG1) continue;
            if (tseg2 < MIN_TSEG2 || tseg2 > MAX_TSEG2) continue;
            
            // SJW should be minimum of TSEG2 and 4
            uint32_t sjw = std::min(tseg2, MAX_SJW);
            
            // Calculate actual values
            double actual_baudrate = tq_freq / total_tq;
            double actual_sample_point = static_cast<double>(1 + tseg1) / total_tq;
            double error = std::abs(actual_baudrate - target_baudrate) / 
                          target_baudrate * 100.0;
            
            candidates.push_back({
                brp, tseg1, tseg2, sjw,
                actual_sample_point, actual_baudrate, error
            });
        }
        
        // Find best candidate (minimum error)
        TimingParams best = candidates[0];
        for (const auto& candidate : candidates) {
            if (candidate.error_percent < best.error_percent) {
                best = candidate;
            }
        }
        
        return best;
    }
    
    /**
     * Print timing configuration
     */
    void print_timing(const TimingParams& params) {
        std::cout << "CAN Bit Timing Configuration:\n";
        std::cout << "  Prescaler (BRP): " << params.brp << "\n";
        std::cout << "  Time Segment 1: " << params.tseg1 << " TQ\n";
        std::cout << "  Time Segment 2: " << params.tseg2 << " TQ\n";
        std::cout << "  Sync Jump Width: " << params.sjw << " TQ\n";
        std::cout << "  Sample Point: " << (params.sample_point * 100) << "%\n";
        std::cout << "  Actual Baudrate: " << params.actual_baudrate << " bps\n";
        std::cout << "  Error: " << params.error_percent << "%\n";
        
        uint32_t total_tq = 1 + params.tseg1 + params.tseg2;
        double tq_duration = 1000000000.0 / (can_clock_ / params.brp);
        std::cout << "  Total TQ per bit: " << total_tq << "\n";
        std::cout << "  TQ Duration: " << tq_duration << " ns\n";
    }
};

// Usage example
int main() {
    CANBitTimingCalculator calc(42000000); // 42 MHz clock
    
    auto timing_500k = calc.calculate_optimal_timing(500000);
    calc.print_timing(timing_500k);
    
    std::cout << "\n";
    
    auto timing_1m = calc.calculate_optimal_timing(1000000);
    calc.print_timing(timing_1m);
    
    return 0;
}
```

---

## Rust Code Examples

### Example 1: Bit Timing Configuration with Embedded HAL

```rust
#![no_std]

/// CAN bit timing configuration
#[derive(Debug, Clone, Copy)]
pub struct BitTiming {
    /// Baud Rate Prescaler (1-1024)
    pub prescaler: u16,
    /// Time Segment 1 (1-16 TQ)
    pub tseg1: u8,
    /// Time Segment 2 (1-8 TQ)
    pub tseg2: u8,
    /// Synchronization Jump Width (1-4 TQ)
    pub sjw: u8,
}

impl BitTiming {
    /// Create new bit timing configuration with validation
    pub fn new(prescaler: u16, tseg1: u8, tseg2: u8, sjw: u8) -> Result<Self, &'static str> {
        if prescaler < 1 || prescaler > 1024 {
            return Err("Prescaler must be 1-1024");
        }
        if tseg1 < 1 || tseg1 > 16 {
            return Err("TSEG1 must be 1-16");
        }
        if tseg2 < 1 || tseg2 > 8 {
            return Err("TSEG2 must be 1-8");
        }
        if sjw < 1 || sjw > 4 || sjw > tseg2 {
            return Err("SJW must be 1-4 and <= TSEG2");
        }
        
        Ok(Self { prescaler, tseg1, tseg2, sjw })
    }
    
    /// Calculate sample point percentage
    pub fn sample_point(&self) -> f32 {
        let total_tq = 1.0 + self.tseg1 as f32 + self.tseg2 as f32;
        (1.0 + self.tseg1 as f32) / total_tq * 100.0
    }
    
    /// Calculate actual baudrate
    pub fn actual_baudrate(&self, can_clock: u32) -> u32 {
        let tq_freq = can_clock / self.prescaler as u32;
        let total_tq = 1 + self.tseg1 as u32 + self.tseg2 as u32;
        tq_freq / total_tq
    }
    
    /// Calculate Time Quantum duration in nanoseconds
    pub fn tq_duration_ns(&self, can_clock: u32) -> u32 {
        1_000_000_000 / (can_clock / self.prescaler as u32)
    }
    
    /// Calculate bit time in nanoseconds
    pub fn bit_time_ns(&self, can_clock: u32) -> u32 {
        let total_tq = 1 + self.tseg1 as u32 + self.tseg2 as u32;
        self.tq_duration_ns(can_clock) * total_tq
    }
}

/// Common CAN baudrate configurations
pub mod presets {
    use super::BitTiming;
    
    /// 500 kbps @ 42 MHz (Sample point: 87.5%)
    pub fn can_500k_42mhz() -> BitTiming {
        BitTiming::new(6, 13, 2, 1).unwrap()
    }
    
    /// 1 Mbps @ 42 MHz (Sample point: 87.5%)
    pub fn can_1m_42mhz() -> BitTiming {
        BitTiming::new(3, 13, 2, 1).unwrap()
    }
    
    /// 250 kbps @ 42 MHz (Sample point: 87.5%)
    pub fn can_250k_42mhz() -> BitTiming {
        BitTiming::new(12, 13, 2, 1).unwrap()
    }
    
    /// 125 kbps @ 42 MHz (Sample point: 87.5%)
    pub fn can_125k_42mhz() -> BitTiming {
        BitTiming::new(24, 13, 2, 1).unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_bit_timing_500k() {
        let timing = presets::can_500k_42mhz();
        assert_eq!(timing.sample_point(), 87.5);
        assert_eq!(timing.actual_baudrate(42_000_000), 500_000);
    }
    
    #[test]
    fn test_validation() {
        assert!(BitTiming::new(0, 13, 2, 1).is_err());
        assert!(BitTiming::new(6, 0, 2, 1).is_err());
        assert!(BitTiming::new(6, 13, 0, 1).is_err());
        assert!(BitTiming::new(6, 13, 2, 5).is_err());
    }
}
```

### Example 2: Advanced Bit Timing Calculator in Rust

```rust
/// Bit timing calculation result
#[derive(Debug, Clone, Copy)]
pub struct TimingResult {
    pub timing: BitTiming,
    pub sample_point: f32,
    pub actual_baudrate: u32,
    pub error_percent: f32,
}

/// CAN bit timing calculator
pub struct BitTimingCalculator {
    can_clock: u32,
}

impl BitTimingCalculator {
    pub fn new(can_clock: u32) -> Self {
        Self { can_clock }
    }
    
    /// Calculate optimal bit timing for target baudrate
    pub fn calculate(
        &self,
        target_baudrate: u32,
        target_sample_point: f32,
    ) -> Option<TimingResult> {
        let mut best_result: Option<TimingResult> = None;
        let mut min_error = f32::MAX;
        
        // Iterate through valid prescaler values
        for brp in 1..=1024u16 {
            let tq_freq = self.can_clock / brp as u32;
            let ntq = (tq_freq as f32) / (target_baudrate as f32);
            
            // Valid bit time: 8-25 TQ
            if ntq < 8.0 || ntq > 25.0 {
                continue;
            }
            
            let total_tq = ntq.round() as u8;
            
            // Calculate segments based on desired sample point
            let tseg1 = ((total_tq as f32 - 1.0) * target_sample_point / 100.0)
                .round() as u8 - 1;
            let tseg2 = total_tq - 1 - tseg1;
            
            // Validate segments
            if tseg1 < 1 || tseg1 > 16 || tseg2 < 1 || tseg2 > 8 {
                continue;
            }
            
            // SJW = min(TSEG2, 4)
            let sjw = tseg2.min(4);
            
            // Create timing configuration
            if let Ok(timing) = BitTiming::new(brp, tseg1, tseg2, sjw) {
                let actual_baudrate = timing.actual_baudrate(self.can_clock);
                let error = ((actual_baudrate as f32 - target_baudrate as f32).abs()
                    / target_baudrate as f32) * 100.0;
                
                if error < min_error {
                    min_error = error;
                    best_result = Some(TimingResult {
                        timing,
                        sample_point: timing.sample_point(),
                        actual_baudrate,
                        error_percent: error,
                    });
                }
            }
        }
        
        best_result
    }
    
    /// Calculate timing with detailed analysis
    pub fn calculate_with_analysis(
        &self,
        target_baudrate: u32,
    ) -> Option<TimingAnalysis> {
        let result = self.calculate(target_baudrate, 87.5)?;
        
        Some(TimingAnalysis {
            result,
            tq_duration_ns: result.timing.tq_duration_ns(self.can_clock),
            bit_time_ns: result.timing.bit_time_ns(self.can_clock),
            max_cable_length_m: self.estimate_max_cable_length(&result.timing),
        })
    }
    
    /// Estimate maximum cable length based on propagation delay
    fn estimate_max_cable_length(&self, timing: &BitTiming) -> f32 {
        // Propagation segment should accommodate twice the cable delay
        // Signal propagation in typical CAN cable: ~5 ns/m
        // Prop_Seg can be part of TSEG1 (typically first 1-8 TQ of TSEG1)
        
        let tq_duration_ns = timing.tq_duration_ns(self.can_clock);
        let prop_seg_tq = timing.tseg1.min(8); // Conservative estimate
        let prop_time_ns = prop_seg_tq as f32 * tq_duration_ns as f32;
        
        // Maximum cable length = (propagation time / 2) / 5 ns/m
        (prop_time_ns / 2.0) / 5.0
    }
}

#[derive(Debug)]
pub struct TimingAnalysis {
    pub result: TimingResult,
    pub tq_duration_ns: u32,
    pub bit_time_ns: u32,
    pub max_cable_length_m: f32,
}

#[cfg(test)]
mod calculator_tests {
    use super::*;
    
    #[test]
    fn test_calculator_500k() {
        let calc = BitTimingCalculator::new(42_000_000);
        let result = calc.calculate(500_000, 87.5).unwrap();
        
        assert!(result.error_percent < 0.1);
        assert!(result.sample_point > 85.0 && result.sample_point < 90.0);
    }
    
    #[test]
    fn test_multiple_baudrates() {
        let calc = BitTimingCalculator::new(42_000_000);
        
        for &baudrate in &[125_000, 250_000, 500_000, 1_000_000] {
            let result = calc.calculate(baudrate, 87.5);
            assert!(result.is_some(), "Failed for baudrate {}", baudrate);
            
            if let Some(r) = result {
                assert!(r.error_percent < 1.0);
            }
        }
    }
}
```

---

## Summary

**CAN Bit Timing and Synchronization** is critical for reliable communication in CAN networks. Key takeaways:

- **Bit Structure**: Each bit comprises four segments (Sync, Prop, Phase1, Phase2) measured in Time Quanta (TQ)
- **Sample Point**: Typically positioned at 75-87.5% of the bit period for optimal noise immunity and timing margin
- **Synchronization**: Nodes use hard synchronization (at SOF) and resynchronization (during frames) to compensate for clock drift
- **SJW**: Allows ±1 to ±4 TQ adjustment per bit to maintain synchronization despite oscillator tolerances
- **Configuration**: Proper calculation of prescaler and segment lengths is essential for achieving target baudrates while maintaining timing margins

The code examples demonstrate practical implementation of bit timing calculations, validation, and configuration in both C/C++ (for embedded systems like STM32) and Rust (with type-safe configurations). The calculators automatically find optimal parameters balancing baudrate accuracy, sample point positioning, and physical network constraints.