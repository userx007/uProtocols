# Profiling SPI Performance

## Overview

Profiling SPI (Serial Peripheral Interface) performance is essential for optimizing communication between microcontrollers and peripheral devices. This involves measuring actual throughput, analyzing latency, and identifying bottlenecks in your SPI implementation. Understanding these metrics helps you make informed decisions about clock speeds, buffer sizes, DMA usage, and overall system architecture.

## Key Performance Metrics

### 1. **Throughput**
The actual data transfer rate, typically measured in bytes per second or bits per second. Theoretical maximum throughput equals the SPI clock frequency, but real-world performance is often lower due to overhead.

### 2. **Latency**
The time between initiating a transfer and receiving the first byte of data. This includes:
- Setup time for chip select and GPIO operations
- DMA initialization overhead
- Interrupt response time
- Software processing delays

### 3. **CPU Utilization**
Percentage of CPU time spent handling SPI operations, which varies significantly between polling, interrupt-driven, and DMA approaches.

### 4. **Common Bottlenecks**
- Slow SPI clock speeds
- Inefficient GPIO operations (especially chip select toggling)
- Interrupt overhead
- Small buffer sizes requiring frequent transfers
- Cache coherency issues with DMA
- Blocking operations preventing concurrent processing

## C/C++ Implementation

Here's a comprehensive example demonstrating SPI performance profiling on an embedded system:

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Platform-specific timer functions (example for ARM Cortex-M)
#ifdef ARM_CORTEX_M
#include "stm32f4xx_hal.h"
#define GET_CYCLE_COUNT() DWT->CYCCNT
#define CYCLES_TO_US(cycles) ((cycles) * 1000000UL / SystemCoreClock)
#else
// Generic fallback using a microsecond timer
extern uint32_t micros(void);
#define GET_CYCLE_COUNT() micros()
#define CYCLES_TO_US(cycles) (cycles)
#endif

// SPI Performance Profiling Structure
typedef struct {
    uint32_t total_bytes;
    uint32_t total_cycles;
    uint32_t transfer_count;
    uint32_t min_latency_cycles;
    uint32_t max_latency_cycles;
    uint32_t cs_toggle_cycles;
    uint32_t dma_setup_cycles;
} spi_profile_t;

static spi_profile_t profile = {0};

// Initialize profiling (enable cycle counter on Cortex-M)
void spi_profile_init(void) {
    #ifdef ARM_CORTEX_M
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    #endif
    
    memset(&profile, 0, sizeof(profile));
    profile.min_latency_cycles = UINT32_MAX;
}

// Measure chip select toggle overhead
uint32_t profile_cs_toggle(void) {
    uint32_t start = GET_CYCLE_COUNT();
    
    // Toggle CS low then high
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);
    
    uint32_t end = GET_CYCLE_COUNT();
    return end - start;
}

// Profiled SPI transfer with polling
void spi_transfer_profiled_polling(uint8_t *tx_data, uint8_t *rx_data, 
                                   uint16_t length) {
    uint32_t start_total = GET_CYCLE_COUNT();
    
    // Measure CS assertion overhead
    uint32_t cs_start = GET_CYCLE_COUNT();
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);
    profile.cs_toggle_cycles += GET_CYCLE_COUNT() - cs_start;
    
    // Measure actual transfer time
    uint32_t transfer_start = GET_CYCLE_COUNT();
    HAL_SPI_TransmitReceive(&hspi1, tx_data, rx_data, length, HAL_MAX_DELAY);
    uint32_t transfer_cycles = GET_CYCLE_COUNT() - transfer_start;
    
    // Measure CS deassertion overhead
    cs_start = GET_CYCLE_COUNT();
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);
    profile.cs_toggle_cycles += GET_CYCLE_COUNT() - cs_start;
    
    uint32_t total_cycles = GET_CYCLE_COUNT() - start_total;
    
    // Update statistics
    profile.total_bytes += length;
    profile.total_cycles += total_cycles;
    profile.transfer_count++;
    
    if (transfer_cycles < profile.min_latency_cycles) {
        profile.min_latency_cycles = transfer_cycles;
    }
    if (transfer_cycles > profile.max_latency_cycles) {
        profile.max_latency_cycles = transfer_cycles;
    }
}

// Profiled SPI transfer with DMA
void spi_transfer_profiled_dma(uint8_t *tx_data, uint8_t *rx_data, 
                               uint16_t length) {
    uint32_t start_total = GET_CYCLE_COUNT();
    
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);
    
    // Measure DMA setup overhead
    uint32_t dma_setup_start = GET_CYCLE_COUNT();
    HAL_SPI_TransmitReceive_DMA(&hspi1, tx_data, rx_data, length);
    profile.dma_setup_cycles += GET_CYCLE_COUNT() - dma_setup_start;
    
    // Wait for completion (in real code, use interrupt/callback)
    while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {}
    
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);
    
    uint32_t total_cycles = GET_CYCLE_COUNT() - start_total;
    
    profile.total_bytes += length;
    profile.total_cycles += total_cycles;
    profile.transfer_count++;
}

// Calculate and print performance statistics
void spi_profile_report(uint32_t cpu_frequency_hz) {
    if (profile.transfer_count == 0) {
        printf("No transfers recorded\n");
        return;
    }
    
    uint32_t avg_cycles = profile.total_cycles / profile.transfer_count;
    uint32_t avg_bytes = profile.total_bytes / profile.transfer_count;
    
    float throughput_bps = (float)profile.total_bytes * cpu_frequency_hz / 
                          profile.total_cycles;
    float throughput_kbps = throughput_bps / 1000.0f;
    
    printf("\n=== SPI Performance Profile ===\n");
    printf("Total transfers: %lu\n", profile.transfer_count);
    printf("Total bytes: %lu\n", profile.total_bytes);
    printf("Average transfer size: %lu bytes\n", avg_bytes);
    printf("\n--- Timing ---\n");
    printf("Average cycles/transfer: %lu (%.2f us)\n", 
           avg_cycles, CYCLES_TO_US(avg_cycles) / 1.0f);
    printf("Min latency: %lu cycles (%.2f us)\n", 
           profile.min_latency_cycles, 
           CYCLES_TO_US(profile.min_latency_cycles) / 1.0f);
    printf("Max latency: %lu cycles (%.2f us)\n", 
           profile.max_latency_cycles,
           CYCLES_TO_US(profile.max_latency_cycles) / 1.0f);
    printf("CS toggle overhead: %lu cycles (%.2f us)\n",
           profile.cs_toggle_cycles,
           CYCLES_TO_US(profile.cs_toggle_cycles) / 1.0f);
    
    if (profile.dma_setup_cycles > 0) {
        printf("DMA setup overhead: %lu cycles (%.2f us)\n",
               profile.dma_setup_cycles,
               CYCLES_TO_US(profile.dma_setup_cycles) / 1.0f);
    }
    
    printf("\n--- Throughput ---\n");
    printf("Effective throughput: %.2f KB/s\n", throughput_kbps);
    printf("Throughput: %.2f Mbps\n", throughput_kbps * 8.0f / 1000.0f);
}

// Benchmark different transfer sizes
void benchmark_transfer_sizes(void) {
    uint16_t sizes[] = {1, 4, 16, 64, 256, 1024};
    uint8_t tx_buffer[1024];
    uint8_t rx_buffer[1024];
    
    printf("\n=== Transfer Size Benchmark ===\n");
    
    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        spi_profile_init();
        
        // Perform 100 transfers of each size
        for (int j = 0; j < 100; j++) {
            spi_transfer_profiled_polling(tx_buffer, rx_buffer, sizes[i]);
        }
        
        printf("\n--- %d bytes per transfer ---\n", sizes[i]);
        spi_profile_report(SystemCoreClock);
    }
}
```

## C++ Object-Oriented Approach

```cpp
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>

class SPIProfiler {
private:
    struct TransferMetrics {
        size_t bytes;
        std::chrono::nanoseconds duration;
        std::chrono::nanoseconds setup_overhead;
    };
    
    std::vector<TransferMetrics> measurements;
    std::chrono::nanoseconds total_cs_overhead{0};
    
public:
    template<typename Func>
    void profileTransfer(size_t bytes, Func&& transfer_fn) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Measure CS overhead
        auto cs_start = std::chrono::high_resolution_clock::now();
        chipSelectLow();
        auto cs_overhead = std::chrono::high_resolution_clock::now() - cs_start;
        
        // Execute actual transfer
        auto transfer_start = std::chrono::high_resolution_clock::now();
        transfer_fn();
        auto transfer_duration = std::chrono::high_resolution_clock::now() - 
                                transfer_start;
        
        chipSelectHigh();
        auto total_duration = std::chrono::high_resolution_clock::now() - start;
        
        total_cs_overhead += cs_overhead;
        measurements.push_back({bytes, transfer_duration, cs_overhead});
    }
    
    void printReport() const {
        if (measurements.empty()) {
            std::cout << "No measurements recorded\n";
            return;
        }
        
        size_t total_bytes = std::accumulate(measurements.begin(), 
                                            measurements.end(), 0ULL,
            [](size_t sum, const auto& m) { return sum + m.bytes; });
        
        auto total_time = std::accumulate(measurements.begin(), 
                                         measurements.end(),
                                         std::chrono::nanoseconds{0},
            [](auto sum, const auto& m) { return sum + m.duration; });
        
        auto min_latency = std::min_element(measurements.begin(), 
                                           measurements.end(),
            [](const auto& a, const auto& b) { 
                return a.duration < b.duration; 
            })->duration;
        
        auto max_latency = std::max_element(measurements.begin(), 
                                           measurements.end(),
            [](const auto& a, const auto& b) { 
                return a.duration < b.duration; 
            })->duration;
        
        double throughput_mbps = (total_bytes * 8.0 * 1e9) / 
                                (total_time.count() * 1e6);
        
        std::cout << "\n=== SPI Performance Report ===\n";
        std::cout << "Total transfers: " << measurements.size() << "\n";
        std::cout << "Total bytes: " << total_bytes << "\n";
        std::cout << "Min latency: " << min_latency.count() << " ns\n";
        std::cout << "Max latency: " << max_latency.count() << " ns\n";
        std::cout << "Avg latency: " << (total_time.count() / measurements.size()) 
                  << " ns\n";
        std::cout << "CS overhead: " << total_cs_overhead.count() << " ns\n";
        std::cout << "Throughput: " << throughput_mbps << " Mbps\n";
    }
    
private:
    void chipSelectLow() { /* GPIO operation */ }
    void chipSelectHigh() { /* GPIO operation */ }
};
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};

#[derive(Debug, Clone)]
struct TransferMetrics {
    bytes: usize,
    duration: Duration,
    setup_overhead: Duration,
}

pub struct SpiProfiler {
    measurements: Vec<TransferMetrics>,
    total_cs_overhead: Duration,
}

impl SpiProfiler {
    pub fn new() -> Self {
        Self {
            measurements: Vec::new(),
            total_cs_overhead: Duration::ZERO,
        }
    }
    
    /// Profile an SPI transfer operation
    pub fn profile_transfer<F>(&mut self, bytes: usize, mut transfer_fn: F)
    where
        F: FnMut(),
    {
        let start = Instant::now();
        
        // Measure chip select overhead
        let cs_start = Instant::now();
        self.chip_select_low();
        let cs_overhead = cs_start.elapsed();
        
        // Execute the actual transfer
        let transfer_start = Instant::now();
        transfer_fn();
        let transfer_duration = transfer_start.elapsed();
        
        self.chip_select_high();
        
        self.total_cs_overhead += cs_overhead;
        self.measurements.push(TransferMetrics {
            bytes,
            duration: transfer_duration,
            setup_overhead: cs_overhead,
        });
    }
    
    /// Generate and print performance report
    pub fn print_report(&self) {
        if self.measurements.is_empty() {
            println!("No measurements recorded");
            return;
        }
        
        let total_bytes: usize = self.measurements.iter()
            .map(|m| m.bytes)
            .sum();
        
        let total_time: Duration = self.measurements.iter()
            .map(|m| m.duration)
            .sum();
        
        let min_latency = self.measurements.iter()
            .map(|m| m.duration)
            .min()
            .unwrap();
        
        let max_latency = self.measurements.iter()
            .map(|m| m.duration)
            .max()
            .unwrap();
        
        let avg_latency = total_time / self.measurements.len() as u32;
        
        // Calculate throughput in Mbps
        let throughput_mbps = (total_bytes as f64 * 8.0 * 1_000_000_000.0) /
                             (total_time.as_nanos() as f64 * 1_000_000.0);
        
        println!("\n=== SPI Performance Report ===");
        println!("Total transfers: {}", self.measurements.len());
        println!("Total bytes: {}", total_bytes);
        println!("Min latency: {:?}", min_latency);
        println!("Max latency: {:?}", max_latency);
        println!("Avg latency: {:?}", avg_latency);
        println!("CS overhead: {:?}", self.total_cs_overhead);
        println!("Throughput: {:.2} Mbps", throughput_mbps);
        println!("Throughput: {:.2} KB/s", throughput_mbps * 1000.0 / 8.0);
    }
    
    /// Benchmark different transfer sizes
    pub fn benchmark_sizes<F>(&mut self, mut transfer_fn: F)
    where
        F: FnMut(usize),
    {
        let sizes = [1, 4, 16, 64, 256, 1024];
        
        println!("\n=== Transfer Size Benchmark ===");
        
        for &size in &sizes {
            // Clear previous measurements
            self.measurements.clear();
            self.total_cs_overhead = Duration::ZERO;
            
            // Perform 100 transfers of each size
            for _ in 0..100 {
                self.profile_transfer(size, || transfer_fn(size));
            }
            
            println!("\n--- {} bytes per transfer ---", size);
            self.print_report();
        }
    }
    
    fn chip_select_low(&self) {
        // Platform-specific GPIO operation
        // e.g., gpio_pin.set_low().unwrap();
    }
    
    fn chip_select_high(&self) {
        // Platform-specific GPIO operation
        // e.g., gpio_pin.set_high().unwrap();
    }
}

// Embedded Rust example using embedded-hal
#[cfg(feature = "embedded")]
mod embedded_example {
    use embedded_hal::blocking::spi::Transfer;
    use embedded_hal::digital::v2::OutputPin;
    use cortex_m::peripheral::DWT;
    
    pub struct EmbeddedSpiProfiler {
        cycle_count_start: u32,
        total_cycles: u64,
        transfer_count: u32,
        min_cycles: u32,
        max_cycles: u32,
    }
    
    impl EmbeddedSpiProfiler {
        pub fn new(dwt: &mut DWT) -> Self {
            // Enable cycle counter
            dwt.enable_cycle_counter();
            
            Self {
                cycle_count_start: 0,
                total_cycles: 0,
                transfer_count: 0,
                min_cycles: u32::MAX,
                max_cycles: 0,
            }
        }
        
        pub fn profile_transfer<SPI, CS, E>(
            &mut self,
            spi: &mut SPI,
            cs: &mut CS,
            dwt: &DWT,
            buffer: &mut [u8],
        ) -> Result<(), E>
        where
            SPI: Transfer<u8, Error = E>,
            CS: OutputPin,
        {
            let start = DWT::cycle_count();
            
            cs.set_low().ok();
            spi.transfer(buffer)?;
            cs.set_high().ok();
            
            let cycles = DWT::cycle_count().wrapping_sub(start);
            
            self.total_cycles += cycles as u64;
            self.transfer_count += 1;
            self.min_cycles = self.min_cycles.min(cycles);
            self.max_cycles = self.max_cycles.max(cycles);
            
            Ok(())
        }
        
        pub fn report(&self, cpu_freq_hz: u32) {
            if self.transfer_count == 0 {
                return;
            }
            
            let avg_cycles = self.total_cycles / self.transfer_count as u64;
            let avg_us = (avg_cycles * 1_000_000) / cpu_freq_hz as u64;
            
            // Print using defmt or similar for embedded
            #[cfg(feature = "defmt")]
            {
                defmt::info!("=== SPI Profile ===");
                defmt::info!("Transfers: {}", self.transfer_count);
                defmt::info!("Avg cycles: {}", avg_cycles);
                defmt::info!("Avg time: {} us", avg_us);
                defmt::info!("Min cycles: {}", self.min_cycles);
                defmt::info!("Max cycles: {}", self.max_cycles);
            }
        }
    }
}

// Example usage
fn main() {
    let mut profiler = SpiProfiler::new();
    
    // Simulate SPI transfers
    profiler.benchmark_sizes(|size| {
        // Simulated transfer delay
        std::thread::sleep(Duration::from_micros(size as u64));
    });
}
```

## Summary

**Profiling SPI performance** is critical for optimizing embedded system communication. Key takeaways include:

- **Measure what matters**: Focus on throughput (actual data rate), latency (response time), and CPU utilization to understand real-world performance.

- **Identify bottlenecks**: Common issues include excessive chip select toggling, interrupt overhead, small buffer sizes, and blocking operations. DMA can significantly improve throughput but adds setup overhead.

- **Use hardware counters**: Cycle counters (like ARM Cortex-M's DWT) provide accurate, low-overhead timing measurements essential for profiling on resource-constrained systems.

- **Benchmark systematically**: Test different transfer sizes, compare polling vs. interrupts vs. DMA, and measure overhead from GPIO operations and protocol setup.

- **Platform considerations**: C/C++ implementations offer direct hardware access ideal for embedded profiling, while Rust provides type safety and zero-cost abstractions with similar performance characteristics.

The provided examples demonstrate complete profiling frameworks that can be adapted to various platforms, helping developers make data-driven optimization decisions for their SPI implementations.