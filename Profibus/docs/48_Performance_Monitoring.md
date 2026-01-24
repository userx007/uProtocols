# Performance Monitoring in Profibus

## Overview

Performance monitoring in Profibus networks is essential for maintaining optimal system operation, preventing downtime, and identifying potential issues before they become critical. This involves continuous tracking of key metrics including bus load, cycle time, error rates, and overall network health.

## Key Performance Metrics

### 1. Bus Load
Bus load represents the percentage of bandwidth being utilized on the Profibus network. High bus load can lead to communication delays and timeouts.

### 2. Cycle Time
The time required to complete one full communication cycle with all connected devices. Consistent cycle times indicate healthy network operation.

### 3. Error Rates
Tracking transmission errors, retry counts, and communication failures helps identify physical layer problems or device malfunctions.

### 4. Network Health Metrics
- Token rotation time
- Gap activity
- Device response times
- Bandwidth utilization per device

## C/C++ Implementation

```cpp
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>

// Performance monitoring structures
typedef struct {
    uint32_t total_frames;
    uint32_t error_frames;
    uint32_t retry_count;
    uint64_t total_bytes;
    double error_rate;
} ProfibusErrorStats;

typedef struct {
    double min_cycle_time_ms;
    double max_cycle_time_ms;
    double avg_cycle_time_ms;
    double current_cycle_time_ms;
    uint32_t cycle_count;
} ProfibusCycleStats;

typedef struct {
    double bus_load_percent;
    double bandwidth_usage_kbps;
    uint32_t active_devices;
    double token_rotation_time_ms;
} ProfibusNetworkStats;

typedef struct {
    ProfibusErrorStats error_stats;
    ProfibusCycleStats cycle_stats;
    ProfibusNetworkStats network_stats;
    time_t monitoring_start_time;
    time_t last_update_time;
} ProfibusPerformanceMonitor;

// Initialize performance monitor
void profibus_monitor_init(ProfibusPerformanceMonitor *monitor) {
    monitor->error_stats.total_frames = 0;
    monitor->error_stats.error_frames = 0;
    monitor->error_stats.retry_count = 0;
    monitor->error_stats.total_bytes = 0;
    monitor->error_stats.error_rate = 0.0;
    
    monitor->cycle_stats.min_cycle_time_ms = 999999.0;
    monitor->cycle_stats.max_cycle_time_ms = 0.0;
    monitor->cycle_stats.avg_cycle_time_ms = 0.0;
    monitor->cycle_stats.current_cycle_time_ms = 0.0;
    monitor->cycle_stats.cycle_count = 0;
    
    monitor->network_stats.bus_load_percent = 0.0;
    monitor->network_stats.bandwidth_usage_kbps = 0.0;
    monitor->network_stats.active_devices = 0;
    monitor->network_stats.token_rotation_time_ms = 0.0;
    
    monitor->monitoring_start_time = time(NULL);
    monitor->last_update_time = time(NULL);
}

// Update cycle time statistics
void profibus_update_cycle_time(ProfibusPerformanceMonitor *monitor, 
                                 double cycle_time_ms) {
    monitor->cycle_stats.current_cycle_time_ms = cycle_time_ms;
    monitor->cycle_stats.cycle_count++;
    
    // Update min/max
    if (cycle_time_ms < monitor->cycle_stats.min_cycle_time_ms) {
        monitor->cycle_stats.min_cycle_time_ms = cycle_time_ms;
    }
    if (cycle_time_ms > monitor->cycle_stats.max_cycle_time_ms) {
        monitor->cycle_stats.max_cycle_time_ms = cycle_time_ms;
    }
    
    // Update average (running average)
    double total = monitor->cycle_stats.avg_cycle_time_ms * 
                   (monitor->cycle_stats.cycle_count - 1);
    monitor->cycle_stats.avg_cycle_time_ms = 
        (total + cycle_time_ms) / monitor->cycle_stats.cycle_count;
}

// Record communication error
void profibus_record_error(ProfibusPerformanceMonitor *monitor, 
                           bool is_retry) {
    monitor->error_stats.error_frames++;
    if (is_retry) {
        monitor->error_stats.retry_count++;
    }
    
    // Calculate error rate
    if (monitor->error_stats.total_frames > 0) {
        monitor->error_stats.error_rate = 
            (double)monitor->error_stats.error_frames / 
            monitor->error_stats.total_frames * 100.0;
    }
}

// Record successful frame transmission
void profibus_record_frame(ProfibusPerformanceMonitor *monitor, 
                           uint32_t frame_size_bytes) {
    monitor->error_stats.total_frames++;
    monitor->error_stats.total_bytes += frame_size_bytes;
    
    // Recalculate error rate
    monitor->error_stats.error_rate = 
        (double)monitor->error_stats.error_frames / 
        monitor->error_stats.total_frames * 100.0;
}

// Calculate bus load
void profibus_calculate_bus_load(ProfibusPerformanceMonitor *monitor, 
                                  uint32_t baud_rate_bps) {
    time_t current_time = time(NULL);
    double elapsed_seconds = difftime(current_time, 
                                      monitor->last_update_time);
    
    if (elapsed_seconds > 0) {
        // Calculate bandwidth usage
        double bits_transmitted = monitor->error_stats.total_bytes * 8.0;
        monitor->network_stats.bandwidth_usage_kbps = 
            (bits_transmitted / elapsed_seconds) / 1000.0;
        
        // Calculate bus load percentage
        monitor->network_stats.bus_load_percent = 
            (monitor->network_stats.bandwidth_usage_kbps * 1000.0 / 
             baud_rate_bps) * 100.0;
    }
    
    monitor->last_update_time = current_time;
}

// Print performance report
void profibus_print_performance_report(ProfibusPerformanceMonitor *monitor) {
    printf("\n=== Profibus Performance Report ===\n");
    printf("\nError Statistics:\n");
    printf("  Total Frames:    %u\n", monitor->error_stats.total_frames);
    printf("  Error Frames:    %u\n", monitor->error_stats.error_frames);
    printf("  Retry Count:     %u\n", monitor->error_stats.retry_count);
    printf("  Error Rate:      %.2f%%\n", monitor->error_stats.error_rate);
    printf("  Total Data:      %llu bytes\n", 
           (unsigned long long)monitor->error_stats.total_bytes);
    
    printf("\nCycle Time Statistics:\n");
    printf("  Current:         %.2f ms\n", 
           monitor->cycle_stats.current_cycle_time_ms);
    printf("  Average:         %.2f ms\n", 
           monitor->cycle_stats.avg_cycle_time_ms);
    printf("  Minimum:         %.2f ms\n", 
           monitor->cycle_stats.min_cycle_time_ms);
    printf("  Maximum:         %.2f ms\n", 
           monitor->cycle_stats.max_cycle_time_ms);
    printf("  Total Cycles:    %u\n", monitor->cycle_stats.cycle_count);
    
    printf("\nNetwork Statistics:\n");
    printf("  Bus Load:        %.2f%%\n", 
           monitor->network_stats.bus_load_percent);
    printf("  Bandwidth Usage: %.2f kbps\n", 
           monitor->network_stats.bandwidth_usage_kbps);
    printf("  Active Devices:  %u\n", 
           monitor->network_stats.active_devices);
    printf("  Token Rotation:  %.2f ms\n", 
           monitor->network_stats.token_rotation_time_ms);
}

// Check for performance issues
bool profibus_check_health(ProfibusPerformanceMonitor *monitor, 
                           double max_error_rate,
                           double max_bus_load,
                           double max_cycle_time) {
    bool healthy = true;
    
    if (monitor->error_stats.error_rate > max_error_rate) {
        printf("WARNING: Error rate %.2f%% exceeds threshold %.2f%%\n",
               monitor->error_stats.error_rate, max_error_rate);
        healthy = false;
    }
    
    if (monitor->network_stats.bus_load_percent > max_bus_load) {
        printf("WARNING: Bus load %.2f%% exceeds threshold %.2f%%\n",
               monitor->network_stats.bus_load_percent, max_bus_load);
        healthy = false;
    }
    
    if (monitor->cycle_stats.current_cycle_time_ms > max_cycle_time) {
        printf("WARNING: Cycle time %.2f ms exceeds threshold %.2f ms\n",
               monitor->cycle_stats.current_cycle_time_ms, max_cycle_time);
        healthy = false;
    }
    
    return healthy;
}

// Example usage
int main() {
    ProfibusPerformanceMonitor monitor;
    profibus_monitor_init(&monitor);
    
    // Simulate monitoring over several cycles
    for (int i = 0; i < 100; i++) {
        // Simulate cycle time (8-12 ms range)
        double cycle_time = 8.0 + (rand() % 40) / 10.0;
        profibus_update_cycle_time(&monitor, cycle_time);
        
        // Simulate frame transmissions
        profibus_record_frame(&monitor, 64);
        
        // Simulate occasional errors
        if (rand() % 50 == 0) {
            profibus_record_error(&monitor, false);
        }
    }
    
    // Calculate bus load (assuming 1.5 Mbps baud rate)
    profibus_calculate_bus_load(&monitor, 1500000);
    monitor.network_stats.active_devices = 8;
    monitor.network_stats.token_rotation_time_ms = 5.2;
    
    // Print report
    profibus_print_performance_report(&monitor);
    
    // Check health
    printf("\n");
    bool healthy = profibus_check_health(&monitor, 5.0, 80.0, 15.0);
    printf("\nNetwork Status: %s\n", healthy ? "HEALTHY" : "ISSUES DETECTED");
    
    return 0;
}
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};
use std::collections::VecDeque;

#[derive(Debug, Clone)]
pub struct ErrorStats {
    pub total_frames: u64,
    pub error_frames: u64,
    pub retry_count: u64,
    pub total_bytes: u64,
    pub error_rate: f64,
}

#[derive(Debug, Clone)]
pub struct CycleStats {
    pub min_cycle_time: Duration,
    pub max_cycle_time: Duration,
    pub avg_cycle_time: Duration,
    pub current_cycle_time: Duration,
    pub cycle_count: u64,
    cycle_time_history: VecDeque<Duration>,
}

#[derive(Debug, Clone)]
pub struct NetworkStats {
    pub bus_load_percent: f64,
    pub bandwidth_usage_kbps: f64,
    pub active_devices: u32,
    pub token_rotation_time: Duration,
}

#[derive(Debug)]
pub struct PerformanceMonitor {
    pub error_stats: ErrorStats,
    pub cycle_stats: CycleStats,
    pub network_stats: NetworkStats,
    monitoring_start: Instant,
    last_update: Instant,
    baud_rate_bps: u32,
}

impl ErrorStats {
    fn new() -> Self {
        Self {
            total_frames: 0,
            error_frames: 0,
            retry_count: 0,
            total_bytes: 0,
            error_rate: 0.0,
        }
    }

    fn calculate_error_rate(&mut self) {
        if self.total_frames > 0 {
            self.error_rate = (self.error_frames as f64 / self.total_frames as f64) * 100.0;
        }
    }
}

impl CycleStats {
    fn new() -> Self {
        Self {
            min_cycle_time: Duration::from_secs(999999),
            max_cycle_time: Duration::ZERO,
            avg_cycle_time: Duration::ZERO,
            current_cycle_time: Duration::ZERO,
            cycle_count: 0,
            cycle_time_history: VecDeque::with_capacity(100),
        }
    }

    fn update(&mut self, cycle_time: Duration) {
        self.current_cycle_time = cycle_time;
        self.cycle_count += 1;

        // Update min/max
        if cycle_time < self.min_cycle_time {
            self.min_cycle_time = cycle_time;
        }
        if cycle_time > self.max_cycle_time {
            self.max_cycle_time = cycle_time;
        }

        // Store in history (keep last 100)
        self.cycle_time_history.push_back(cycle_time);
        if self.cycle_time_history.len() > 100 {
            self.cycle_time_history.pop_front();
        }

        // Calculate rolling average
        let sum: Duration = self.cycle_time_history.iter().sum();
        self.avg_cycle_time = sum / self.cycle_time_history.len() as u32;
    }

    pub fn get_jitter(&self) -> Duration {
        self.max_cycle_time.saturating_sub(self.min_cycle_time)
    }

    pub fn get_std_deviation(&self) -> f64 {
        if self.cycle_time_history.len() < 2 {
            return 0.0;
        }

        let mean = self.avg_cycle_time.as_secs_f64();
        let variance: f64 = self.cycle_time_history
            .iter()
            .map(|t| {
                let diff = t.as_secs_f64() - mean;
                diff * diff
            })
            .sum::<f64>() / self.cycle_time_history.len() as f64;

        variance.sqrt()
    }
}

impl NetworkStats {
    fn new() -> Self {
        Self {
            bus_load_percent: 0.0,
            bandwidth_usage_kbps: 0.0,
            active_devices: 0,
            token_rotation_time: Duration::ZERO,
        }
    }
}

impl PerformanceMonitor {
    pub fn new(baud_rate_bps: u32) -> Self {
        let now = Instant::now();
        Self {
            error_stats: ErrorStats::new(),
            cycle_stats: CycleStats::new(),
            network_stats: NetworkStats::new(),
            monitoring_start: now,
            last_update: now,
            baud_rate_bps,
        }
    }

    pub fn record_frame(&mut self, frame_size_bytes: u64) {
        self.error_stats.total_frames += 1;
        self.error_stats.total_bytes += frame_size_bytes;
        self.error_stats.calculate_error_rate();
    }

    pub fn record_error(&mut self, is_retry: bool) {
        self.error_stats.error_frames += 1;
        if is_retry {
            self.error_stats.retry_count += 1;
        }
        self.error_stats.calculate_error_rate();
    }

    pub fn update_cycle_time(&mut self, cycle_time: Duration) {
        self.cycle_stats.update(cycle_time);
    }

    pub fn calculate_bus_load(&mut self) {
        let elapsed = self.last_update.elapsed();
        let elapsed_secs = elapsed.as_secs_f64();

        if elapsed_secs > 0.0 {
            let bits_transmitted = self.error_stats.total_bytes as f64 * 8.0;
            self.network_stats.bandwidth_usage_kbps = 
                (bits_transmitted / elapsed_secs) / 1000.0;

            self.network_stats.bus_load_percent = 
                (self.network_stats.bandwidth_usage_kbps * 1000.0 / 
                 self.baud_rate_bps as f64) * 100.0;
        }

        self.last_update = Instant::now();
    }

    pub fn set_active_devices(&mut self, count: u32) {
        self.network_stats.active_devices = count;
    }

    pub fn set_token_rotation_time(&mut self, time: Duration) {
        self.network_stats.token_rotation_time = time;
    }

    pub fn check_health(&self, thresholds: &HealthThresholds) -> HealthStatus {
        let mut issues = Vec::new();

        if self.error_stats.error_rate > thresholds.max_error_rate {
            issues.push(format!(
                "Error rate {:.2}% exceeds threshold {:.2}%",
                self.error_stats.error_rate, thresholds.max_error_rate
            ));
        }

        if self.network_stats.bus_load_percent > thresholds.max_bus_load {
            issues.push(format!(
                "Bus load {:.2}% exceeds threshold {:.2}%",
                self.network_stats.bus_load_percent, thresholds.max_bus_load
            ));
        }

        if self.cycle_stats.current_cycle_time > thresholds.max_cycle_time {
            issues.push(format!(
                "Cycle time {:.2}ms exceeds threshold {:.2}ms",
                self.cycle_stats.current_cycle_time.as_secs_f64() * 1000.0,
                thresholds.max_cycle_time.as_secs_f64() * 1000.0
            ));
        }

        if issues.is_empty() {
            HealthStatus::Healthy
        } else {
            HealthStatus::Degraded(issues)
        }
    }

    pub fn print_report(&self) {
        println!("\n=== Profibus Performance Report ===");
        
        println!("\nError Statistics:");
        println!("  Total Frames:    {}", self.error_stats.total_frames);
        println!("  Error Frames:    {}", self.error_stats.error_frames);
        println!("  Retry Count:     {}", self.error_stats.retry_count);
        println!("  Error Rate:      {:.2}%", self.error_stats.error_rate);
        println!("  Total Data:      {} bytes", self.error_stats.total_bytes);

        println!("\nCycle Time Statistics:");
        println!("  Current:         {:.2} ms", 
                 self.cycle_stats.current_cycle_time.as_secs_f64() * 1000.0);
        println!("  Average:         {:.2} ms", 
                 self.cycle_stats.avg_cycle_time.as_secs_f64() * 1000.0);
        println!("  Minimum:         {:.2} ms", 
                 self.cycle_stats.min_cycle_time.as_secs_f64() * 1000.0);
        println!("  Maximum:         {:.2} ms", 
                 self.cycle_stats.max_cycle_time.as_secs_f64() * 1000.0);
        println!("  Jitter:          {:.2} ms", 
                 self.cycle_stats.get_jitter().as_secs_f64() * 1000.0);
        println!("  Std Deviation:   {:.2} ms", 
                 self.cycle_stats.get_std_deviation() * 1000.0);
        println!("  Total Cycles:    {}", self.cycle_stats.cycle_count);

        println!("\nNetwork Statistics:");
        println!("  Bus Load:        {:.2}%", 
                 self.network_stats.bus_load_percent);
        println!("  Bandwidth Usage: {:.2} kbps", 
                 self.network_stats.bandwidth_usage_kbps);
        println!("  Active Devices:  {}", 
                 self.network_stats.active_devices);
        println!("  Token Rotation:  {:.2} ms", 
                 self.network_stats.token_rotation_time.as_secs_f64() * 1000.0);
        
        let uptime = self.monitoring_start.elapsed();
        println!("\nMonitoring Uptime: {:.2} seconds", 
                 uptime.as_secs_f64());
    }
}

#[derive(Debug)]
pub struct HealthThresholds {
    pub max_error_rate: f64,
    pub max_bus_load: f64,
    pub max_cycle_time: Duration,
}

#[derive(Debug)]
pub enum HealthStatus {
    Healthy,
    Degraded(Vec<String>),
}

// Example usage
fn main() {
    use rand::Rng;
    
    let mut monitor = PerformanceMonitor::new(1_500_000); // 1.5 Mbps
    let mut rng = rand::thread_rng();

    // Simulate monitoring
    for _ in 0..100 {
        let cycle_time_ms = 8.0 + rng.gen_range(0.0..4.0);
        let cycle_time = Duration::from_secs_f64(cycle_time_ms / 1000.0);
        
        monitor.update_cycle_time(cycle_time);
        monitor.record_frame(64);

        // Simulate occasional errors
        if rng.gen_range(0..50) == 0 {
            monitor.record_error(false);
        }

        std::thread::sleep(Duration::from_millis(10));
    }

    monitor.calculate_bus_load();
    monitor.set_active_devices(8);
    monitor.set_token_rotation_time(Duration::from_secs_f64(0.0052));

    monitor.print_report();

    let thresholds = HealthThresholds {
        max_error_rate: 5.0,
        max_bus_load: 80.0,
        max_cycle_time: Duration::from_millis(15),
    };

    match monitor.check_health(&thresholds) {
        HealthStatus::Healthy => println!("\nNetwork Status: HEALTHY"),
        HealthStatus::Degraded(issues) => {
            println!("\nNetwork Status: ISSUES DETECTED");
            for issue in issues {
                println!("  - {}", issue);
            }
        }
    }
}
```

## Summary

Performance monitoring is critical for maintaining reliable Profibus networks. The implementations above provide comprehensive tracking of:

- **Error metrics**: Frame errors, retries, and overall error rates help identify communication problems
- **Cycle time analysis**: Min/max/average cycle times, jitter, and standard deviation reveal timing inconsistencies
- **Network utilization**: Bus load and bandwidth calculations ensure the network isn't oversaturated
- **Health checks**: Automated threshold monitoring alerts operators to degrading conditions

The C/C++ implementation focuses on efficiency and direct hardware integration, suitable for embedded systems. The Rust implementation adds memory safety, more sophisticated statistics (jitter, standard deviation), and modern error handling patterns. Both provide real-time visibility into network performance, enabling proactive maintenance and troubleshooting before issues impact production systems.