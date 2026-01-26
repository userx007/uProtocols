# Profiling CAN Stack Performance

## Overview

Profiling a CAN (Controller Area Network) stack is essential for ensuring real-time performance, meeting timing constraints, and optimizing resource utilization in embedded systems. This involves measuring key metrics such as latency, throughput, and CPU utilization to identify bottlenecks and validate that the system meets its requirements.

## Key Performance Metrics

### 1. **Latency**
- **End-to-end latency**: Time from message initiation to reception
- **Transmission latency**: Time from queuing to bus transmission
- **Reception latency**: Time from bus reception to application processing
- **Interrupt latency**: Time from hardware interrupt to ISR execution

### 2. **Throughput**
- **Bus utilization**: Percentage of available bandwidth used
- **Messages per second**: Rate of successful message transmission/reception
- **Data rate**: Actual data throughput in bytes/second

### 3. **CPU Utilization**
- **ISR overhead**: CPU time spent in interrupt service routines
- **Stack processing**: Time spent in CAN driver and protocol layers
- **Idle time**: Available CPU capacity for application tasks

## Profiling Techniques

### Hardware-Based Profiling
- **GPIO toggling**: Toggle pins at entry/exit points for oscilloscope measurement
- **Hardware timers**: Use dedicated timers for precise timing measurements
- **Trace buffers**: Capture execution traces with minimal intrusion

### Software-Based Profiling
- **Timestamp logging**: Record timestamps at key execution points
- **Cycle counters**: Use CPU cycle counters (DWT on ARM Cortex-M)
- **Statistical sampling**: Periodically sample program counter for hotspot analysis

## C/C++ Implementation Examples

### Basic Latency Measurement with GPIO Toggling

```c
#include <stdint.h>
#include <stdbool.h>

// GPIO definitions for profiling
#define PROFILE_PIN_TX_START    GPIO_PIN_0
#define PROFILE_PIN_TX_END      GPIO_PIN_1
#define PROFILE_PIN_RX_START    GPIO_PIN_2
#define PROFILE_PIN_RX_END      GPIO_PIN_3

// Inline functions for minimal overhead
static inline void profile_pin_set(uint32_t pin) {
    GPIOA->BSRR = pin;  // Set pin high
}

static inline void profile_pin_clear(uint32_t pin) {
    GPIOA->BSRR = (pin << 16);  // Set pin low
}

// CAN transmission with profiling
bool can_transmit_profiled(uint32_t id, const uint8_t *data, uint8_t len) {
    profile_pin_set(PROFILE_PIN_TX_START);
    
    bool result = can_transmit(id, data, len);
    
    profile_pin_clear(PROFILE_PIN_TX_START);
    return result;
}

// CAN ISR with profiling
void CAN1_TX_IRQHandler(void) {
    profile_pin_set(PROFILE_PIN_TX_END);
    
    // Handle transmission complete interrupt
    can_handle_tx_interrupt();
    
    profile_pin_clear(PROFILE_PIN_TX_END);
}
```

### Cycle Counter-Based Profiling (ARM Cortex-M)

```c
#include <stdint.h>

// DWT (Data Watchpoint and Trace) cycle counter
#define DWT_CYCCNT      (*((volatile uint32_t *)0xE0001004))
#define DWT_CONTROL     (*((volatile uint32_t *)0xE0001000))
#define DWT_LAR         (*((volatile uint32_t *)0xE0001FB0))
#define SCB_DEMCR       (*((volatile uint32_t *)0xE000EDFC))

typedef struct {
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t total_cycles;
    uint32_t count;
} timing_stats_t;

typedef struct {
    timing_stats_t tx_latency;
    timing_stats_t rx_latency;
    timing_stats_t isr_duration;
    uint32_t messages_sent;
    uint32_t messages_received;
} can_profiling_data_t;

static can_profiling_data_t profiling_data = {0};

// Initialize cycle counter
void profiling_init(void) {
    // Enable trace
    SCB_DEMCR |= 0x01000000;
    
    // Unlock DWT
    DWT_LAR = 0xC5ACCE55;
    
    // Enable cycle counter
    DWT_CONTROL |= 1;
    
    // Reset counter
    DWT_CYCCNT = 0;
}

// Get current cycle count
static inline uint32_t get_cycles(void) {
    return DWT_CYCCNT;
}

// Update timing statistics
void update_timing_stats(timing_stats_t *stats, uint32_t cycles) {
    if (stats->count == 0) {
        stats->min_cycles = cycles;
        stats->max_cycles = cycles;
    } else {
        if (cycles < stats->min_cycles) stats->min_cycles = cycles;
        if (cycles > stats->max_cycles) stats->max_cycles = cycles;
    }
    
    stats->total_cycles += cycles;
    stats->count++;
}

// Profiled CAN transmission
bool can_transmit_with_profiling(uint32_t id, const uint8_t *data, uint8_t len) {
    uint32_t start_cycles = get_cycles();
    
    bool result = can_transmit(id, data, len);
    
    uint32_t end_cycles = get_cycles();
    uint32_t elapsed = end_cycles - start_cycles;
    
    update_timing_stats(&profiling_data.tx_latency, elapsed);
    profiling_data.messages_sent++;
    
    return result;
}

// Get average cycles
uint32_t get_average_cycles(const timing_stats_t *stats) {
    if (stats->count == 0) return 0;
    return (uint32_t)(stats->total_cycles / stats->count);
}

// Print profiling results
void print_profiling_stats(void) {
    printf("CAN Stack Profiling Results:\n");
    printf("TX Latency: min=%lu, max=%lu, avg=%lu cycles\n",
           profiling_data.tx_latency.min_cycles,
           profiling_data.tx_latency.max_cycles,
           get_average_cycles(&profiling_data.tx_latency));
    
    printf("RX Latency: min=%lu, max=%lu, avg=%lu cycles\n",
           profiling_data.rx_latency.min_cycles,
           profiling_data.rx_latency.max_cycles,
           get_average_cycles(&profiling_data.rx_latency));
    
    printf("Messages: sent=%lu, received=%lu\n",
           profiling_data.messages_sent,
           profiling_data.messages_received);
}
```

### C++ RAII Profiling Wrapper

```cpp
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>

class CANProfiler {
public:
    struct Measurement {
        std::chrono::nanoseconds duration;
        std::chrono::steady_clock::time_point timestamp;
    };
    
private:
    std::vector<Measurement> tx_measurements_;
    std::vector<Measurement> rx_measurements_;
    uint32_t cpu_frequency_mhz_;
    
public:
    CANProfiler(uint32_t cpu_freq_mhz = 168) 
        : cpu_frequency_mhz_(cpu_freq_mhz) {
        tx_measurements_.reserve(10000);
        rx_measurements_.reserve(10000);
    }
    
    // RAII scope timer
    class ScopedTimer {
    private:
        CANProfiler& profiler_;
        std::vector<Measurement>& measurements_;
        std::chrono::steady_clock::time_point start_;
        
    public:
        ScopedTimer(CANProfiler& profiler, std::vector<Measurement>& measurements)
            : profiler_(profiler), measurements_(measurements),
              start_(std::chrono::steady_clock::now()) {}
        
        ~ScopedTimer() {
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start_);
            measurements_.push_back({duration, start_});
        }
    };
    
    ScopedTimer measureTx() {
        return ScopedTimer(*this, tx_measurements_);
    }
    
    ScopedTimer measureRx() {
        return ScopedTimer(*this, rx_measurements_);
    }
    
    struct Statistics {
        double min_us;
        double max_us;
        double avg_us;
        double stddev_us;
        size_t count;
    };
    
    Statistics calculateStats(const std::vector<Measurement>& measurements) const {
        if (measurements.empty()) {
            return {0, 0, 0, 0, 0};
        }
        
        std::vector<double> durations_us;
        durations_us.reserve(measurements.size());
        
        for (const auto& m : measurements) {
            durations_us.push_back(m.duration.count() / 1000.0);
        }
        
        auto [min_it, max_it] = std::minmax_element(
            durations_us.begin(), durations_us.end());
        
        double avg = std::accumulate(durations_us.begin(), durations_us.end(), 0.0) 
                     / durations_us.size();
        
        double sq_sum = std::inner_product(
            durations_us.begin(), durations_us.end(),
            durations_us.begin(), 0.0);
        double stddev = std::sqrt(sq_sum / durations_us.size() - avg * avg);
        
        return {*min_it, *max_it, avg, stddev, durations_us.size()};
    }
    
    void printReport() const {
        std::cout << "\n=== CAN Stack Performance Report ===\n";
        
        auto tx_stats = calculateStats(tx_measurements_);
        std::cout << "\nTransmission Latency:\n";
        std::cout << "  Count:  " << tx_stats.count << "\n";
        std::cout << "  Min:    " << tx_stats.min_us << " µs\n";
        std::cout << "  Max:    " << tx_stats.max_us << " µs\n";
        std::cout << "  Avg:    " << tx_stats.avg_us << " µs\n";
        std::cout << "  StdDev: " << tx_stats.stddev_us << " µs\n";
        
        auto rx_stats = calculateStats(rx_measurements_);
        std::cout << "\nReception Latency:\n";
        std::cout << "  Count:  " << rx_stats.count << "\n";
        std::cout << "  Min:    " << rx_stats.min_us << " µs\n";
        std::cout << "  Max:    " << rx_stats.max_us << " µs\n";
        std::cout << "  Avg:    " << rx_stats.avg_us << " µs\n";
        std::cout << "  StdDev: " << rx_stats.stddev_us << " µs\n";
    }
    
    void reset() {
        tx_measurements_.clear();
        rx_measurements_.clear();
    }
};

// Usage example
CANProfiler profiler;

bool sendCANMessage(uint32_t id, const uint8_t* data, size_t len) {
    auto timer = profiler.measureTx();
    // Actual CAN transmission code
    return can_low_level_transmit(id, data, len);
}

void onCANMessageReceived(uint32_t id, const uint8_t* data, size_t len) {
    auto timer = profiler.measureRx();
    // Process received message
    process_can_message(id, data, len);
}
```

## Rust Implementation Examples

### Embedded Rust Profiling with no_std

```rust
#![no_std]

use core::sync::atomic::{AtomicU32, Ordering};

// Cycle counter access (ARM Cortex-M)
pub struct CycleCounter;

impl CycleCounter {
    const DWT_CYCCNT: *mut u32 = 0xE000_1004 as *mut u32;
    const DWT_CONTROL: *mut u32 = 0xE000_1000 as *mut u32;
    const SCB_DEMCR: *mut u32 = 0xE000_EDFC as *mut u32;
    
    pub fn init() {
        unsafe {
            // Enable trace
            core::ptr::write_volatile(Self::SCB_DEMCR, 
                core::ptr::read_volatile(Self::SCB_DEMCR) | (1 << 24));
            
            // Enable cycle counter
            core::ptr::write_volatile(Self::DWT_CONTROL, 
                core::ptr::read_volatile(Self::DWT_CONTROL) | 1);
            
            // Reset counter
            core::ptr::write_volatile(Self::DWT_CYCCNT, 0);
        }
    }
    
    #[inline(always)]
    pub fn get() -> u32 {
        unsafe { core::ptr::read_volatile(Self::DWT_CYCCNT) }
    }
}

#[derive(Debug, Default)]
pub struct TimingStats {
    min_cycles: AtomicU32,
    max_cycles: AtomicU32,
    total_cycles: AtomicU32,
    count: AtomicU32,
}

impl TimingStats {
    pub fn new() -> Self {
        Self {
            min_cycles: AtomicU32::new(u32::MAX),
            max_cycles: AtomicU32::new(0),
            total_cycles: AtomicU32::new(0),
            count: AtomicU32::new(0),
        }
    }
    
    pub fn record(&self, cycles: u32) {
        // Update min atomically
        let mut current_min = self.min_cycles.load(Ordering::Relaxed);
        while cycles < current_min {
            match self.min_cycles.compare_exchange_weak(
                current_min,
                cycles,
                Ordering::Release,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(x) => current_min = x,
            }
        }
        
        // Update max atomically
        let mut current_max = self.max_cycles.load(Ordering::Relaxed);
        while cycles > current_max {
            match self.max_cycles.compare_exchange_weak(
                current_max,
                cycles,
                Ordering::Release,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(x) => current_max = x,
            }
        }
        
        self.total_cycles.fetch_add(cycles, Ordering::Relaxed);
        self.count.fetch_add(1, Ordering::Relaxed);
    }
    
    pub fn average(&self) -> u32 {
        let count = self.count.load(Ordering::Relaxed);
        if count == 0 {
            return 0;
        }
        self.total_cycles.load(Ordering::Relaxed) / count
    }
    
    pub fn min(&self) -> u32 {
        let min = self.min_cycles.load(Ordering::Relaxed);
        if min == u32::MAX { 0 } else { min }
    }
    
    pub fn max(&self) -> u32 {
        self.max_cycles.load(Ordering::Relaxed)
    }
    
    pub fn count(&self) -> u32 {
        self.count.load(Ordering::Relaxed)
    }
}

pub struct CANProfiler {
    tx_latency: TimingStats,
    rx_latency: TimingStats,
    isr_duration: TimingStats,
}

impl CANProfiler {
    pub fn new() -> Self {
        CycleCounter::init();
        Self {
            tx_latency: TimingStats::new(),
            rx_latency: TimingStats::new(),
            isr_duration: TimingStats::new(),
        }
    }
    
    #[inline(always)]
    pub fn measure_tx<F, R>(&self, f: F) -> R
    where
        F: FnOnce() -> R,
    {
        let start = CycleCounter::get();
        let result = f();
        let end = CycleCounter::get();
        
        self.tx_latency.record(end.wrapping_sub(start));
        result
    }
    
    #[inline(always)]
    pub fn measure_rx<F, R>(&self, f: F) -> R
    where
        F: FnOnce() -> R,
    {
        let start = CycleCounter::get();
        let result = f();
        let end = CycleCounter::get();
        
        self.rx_latency.record(end.wrapping_sub(start));
        result
    }
    
    pub fn print_stats(&self, cpu_freq_mhz: u32) {
        defmt::info!("=== CAN Stack Performance ===");
        
        let tx_avg_us = self.tx_latency.average() / cpu_freq_mhz;
        defmt::info!(
            "TX: min={} max={} avg={} µs (count={})",
            self.tx_latency.min() / cpu_freq_mhz,
            self.tx_latency.max() / cpu_freq_mhz,
            tx_avg_us,
            self.tx_latency.count()
        );
        
        let rx_avg_us = self.rx_latency.average() / cpu_freq_mhz;
        defmt::info!(
            "RX: min={} max={} avg={} µs (count={})",
            self.rx_latency.min() / cpu_freq_mhz,
            self.rx_latency.max() / cpu_freq_mhz,
            rx_avg_us,
            self.rx_latency.count()
        );
    }
}

// Usage example
static PROFILER: CANProfiler = CANProfiler::new();

pub fn can_transmit_profiled(id: u32, data: &[u8]) -> Result<(), CANError> {
    PROFILER.measure_tx(|| {
        can_transmit(id, data)
    })
}

pub fn can_receive_handler(id: u32, data: &[u8]) {
    PROFILER.measure_rx(|| {
        process_can_message(id, data)
    });
}
```

### Advanced Rust Profiling with Statistics

```rust
use std::time::{Duration, Instant};
use std::collections::VecDeque;

pub struct PerformanceMetrics {
    samples: VecDeque<Duration>,
    max_samples: usize,
}

impl PerformanceMetrics {
    pub fn new(max_samples: usize) -> Self {
        Self {
            samples: VecDeque::with_capacity(max_samples),
            max_samples,
        }
    }
    
    pub fn record(&mut self, duration: Duration) {
        if self.samples.len() >= self.max_samples {
            self.samples.pop_front();
        }
        self.samples.push_back(duration);
    }
    
    pub fn min(&self) -> Option<Duration> {
        self.samples.iter().min().copied()
    }
    
    pub fn max(&self) -> Option<Duration> {
        self.samples.iter().max().copied()
    }
    
    pub fn average(&self) -> Option<Duration> {
        if self.samples.is_empty() {
            return None;
        }
        
        let sum: Duration = self.samples.iter().sum();
        Some(sum / self.samples.len() as u32)
    }
    
    pub fn percentile(&self, p: f64) -> Option<Duration> {
        if self.samples.is_empty() {
            return None;
        }
        
        let mut sorted: Vec<_> = self.samples.iter().copied().collect();
        sorted.sort();
        
        let index = ((p / 100.0) * (sorted.len() - 1) as f64).round() as usize;
        Some(sorted[index])
    }
    
    pub fn standard_deviation(&self) -> Option<f64> {
        if self.samples.is_empty() {
            return None;
        }
        
        let avg = self.average()?.as_secs_f64();
        let variance = self.samples
            .iter()
            .map(|d| {
                let diff = d.as_secs_f64() - avg;
                diff * diff
            })
            .sum::<f64>() / self.samples.len() as f64;
        
        Some(variance.sqrt())
    }
}

pub struct CANStackProfiler {
    tx_metrics: PerformanceMetrics,
    rx_metrics: PerformanceMetrics,
    messages_sent: u64,
    messages_received: u64,
    start_time: Instant,
}

impl CANStackProfiler {
    pub fn new(sample_size: usize) -> Self {
        Self {
            tx_metrics: PerformanceMetrics::new(sample_size),
            rx_metrics: PerformanceMetrics::new(sample_size),
            messages_sent: 0,
            messages_received: 0,
            start_time: Instant::now(),
        }
    }
    
    pub fn profile_tx<F, R>(&mut self, operation: F) -> R
    where
        F: FnOnce() -> R,
    {
        let start = Instant::now();
        let result = operation();
        let duration = start.elapsed();
        
        self.tx_metrics.record(duration);
        self.messages_sent += 1;
        
        result
    }
    
    pub fn profile_rx<F, R>(&mut self, operation: F) -> R
    where
        F: FnOnce() -> R,
    {
        let start = Instant::now();
        let result = operation();
        let duration = start.elapsed();
        
        self.rx_metrics.record(duration);
        self.messages_received += 1;
        
        result
    }
    
    pub fn throughput(&self) -> f64 {
        let elapsed = self.start_time.elapsed().as_secs_f64();
        if elapsed == 0.0 {
            return 0.0;
        }
        (self.messages_sent + self.messages_received) as f64 / elapsed
    }
    
    pub fn print_report(&self) {
        println!("\n╔═══════════════════════════════════════════╗");
        println!("║     CAN Stack Performance Report         ║");
        println!("╚═══════════════════════════════════════════╝\n");
        
        println!("Transmission Metrics:");
        if let Some(avg) = self.tx_metrics.average() {
            println!("  Average:  {:?}", avg);
            println!("  Min:      {:?}", self.tx_metrics.min().unwrap());
            println!("  Max:      {:?}", self.tx_metrics.max().unwrap());
            println!("  P50:      {:?}", self.tx_metrics.percentile(50.0).unwrap());
            println!("  P95:      {:?}", self.tx_metrics.percentile(95.0).unwrap());
            println!("  P99:      {:?}", self.tx_metrics.percentile(99.0).unwrap());
            if let Some(stddev) = self.tx_metrics.standard_deviation() {
                println!("  StdDev:   {:.2} µs", stddev * 1_000_000.0);
            }
        }
        
        println!("\nReception Metrics:");
        if let Some(avg) = self.rx_metrics.average() {
            println!("  Average:  {:?}", avg);
            println!("  Min:      {:?}", self.rx_metrics.min().unwrap());
            println!("  Max:      {:?}", self.rx_metrics.max().unwrap());
            println!("  P50:      {:?}", self.rx_metrics.percentile(50.0).unwrap());
            println!("  P95:      {:?}", self.rx_metrics.percentile(95.0).unwrap());
            println!("  P99:      {:?}", self.rx_metrics.percentile(99.0).unwrap());
            if let Some(stddev) = self.rx_metrics.standard_deviation() {
                println!("  StdDev:   {:.2} µs", stddev * 1_000_000.0);
            }
        }
        
        println!("\nThroughput:");
        println!("  Messages sent:     {}", self.messages_sent);
        println!("  Messages received: {}", self.messages_received);
        println!("  Total throughput:  {:.2} msg/s", self.throughput());
        println!("  Runtime:           {:?}", self.start_time.elapsed());
    }
}

// Usage example
fn main() {
    let mut profiler = CANStackProfiler::new(10000);
    
    // Simulate CAN operations
    for i in 0..1000 {
        profiler.profile_tx(|| {
            // Simulate CAN transmission
            std::thread::sleep(Duration::from_micros(50));
        });
        
        profiler.profile_rx(|| {
            // Simulate CAN reception
            std::thread::sleep(Duration::from_micros(30));
        });
    }
    
    profiler.print_report();
}
```

## Analysis and Optimization Strategies

### Common Bottlenecks

1. **Interrupt overhead**: Too much processing in ISRs
2. **Buffer management**: Inefficient queue operations
3. **Context switching**: Excessive task switches
4. **Memory allocation**: Dynamic allocation in critical paths
5. **Protocol overhead**: Unnecessary copying or processing

### Optimization Techniques

1. **Minimize ISR duration**: Defer processing to tasks
2. **Use lock-free queues**: Reduce contention and latency
3. **Optimize buffer sizes**: Balance memory and performance
4. **Enable hardware filtering**: Reduce software filtering overhead
5. **Use DMA**: Offload data transfers from CPU

## Summary

Profiling CAN stack performance is critical for embedded systems requiring real-time guarantees. Key techniques include:

- **Hardware-assisted profiling** using GPIO toggling and cycle counters provides minimal overhead and accurate measurements
- **Software profiling** with timestamps and statistical analysis helps identify performance bottlenecks and validate requirements
- **C/C++ implementations** leverage low-level hardware access and RAII patterns for automatic measurement
- **Rust implementations** provide memory-safe profiling with zero-cost abstractions and atomic operations for concurrent scenarios

The profiling data collected (latency, throughput, CPU utilization) enables developers to:
- Validate timing requirements
- Identify optimization opportunities
- Ensure deterministic behavior
- Size hardware resources appropriately
- Debug performance regressions

Regular profiling throughout development ensures the CAN stack meets its performance targets and provides reliable, predictable operation in production systems.