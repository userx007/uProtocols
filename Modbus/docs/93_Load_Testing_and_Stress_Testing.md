# Load Testing and Stress Testing in Modbus Systems

## Overview

Load testing and stress testing are critical validation techniques for ensuring Modbus-based industrial control systems can handle expected operational demands and extreme conditions. These testing methodologies help identify performance bottlenecks, resource limitations, and system breaking points before deployment in production environments.

## Detailed Description

### Load Testing

Load testing involves subjecting a Modbus system to expected operational workloads to verify it can handle normal and peak traffic conditions. This includes:

- **Transaction Rate Testing**: Validating the system can process the required number of Modbus transactions per second
- **Concurrent Connection Testing**: Ensuring multiple Modbus clients can connect and communicate simultaneously
- **Sustained Operation Testing**: Verifying stable performance over extended periods
- **Response Time Measurement**: Monitoring how quickly the system responds under various loads

### Stress Testing

Stress testing pushes the system beyond normal operational limits to identify breaking points and failure modes:

- **Maximum Capacity Testing**: Determining the absolute maximum number of transactions or connections
- **Resource Exhaustion Testing**: Identifying what happens when memory, CPU, or network resources are depleted
- **Recovery Testing**: Validating the system can recover gracefully from overload conditions
- **Degradation Analysis**: Understanding how performance degrades as load increases

### Key Metrics

- **Throughput**: Transactions per second (TPS)
- **Latency**: Response time for individual requests
- **Error Rate**: Percentage of failed transactions
- **Resource Utilization**: CPU, memory, network bandwidth
- **Connection Stability**: Connection drops and reconnection success rate

## C/C++ Implementation

### Load Testing Framework

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <modbus/modbus.h>
#include <unistd.h>
#include <sys/time.h>

// Test configuration structure
typedef struct {
    char *ip_address;
    int port;
    int num_threads;
    int requests_per_thread;
    int register_address;
    int num_registers;
} test_config_t;

// Thread statistics
typedef struct {
    unsigned long successful_requests;
    unsigned long failed_requests;
    double total_response_time;
    double min_response_time;
    double max_response_time;
    pthread_mutex_t lock;
} thread_stats_t;

// Global statistics
thread_stats_t global_stats = {
    .successful_requests = 0,
    .failed_requests = 0,
    .total_response_time = 0.0,
    .min_response_time = 999999.0,
    .max_response_time = 0.0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// Get current time in milliseconds
double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

// Update statistics thread-safely
void update_stats(int success, double response_time) {
    pthread_mutex_lock(&global_stats.lock);
    
    if (success) {
        global_stats.successful_requests++;
        global_stats.total_response_time += response_time;
        
        if (response_time < global_stats.min_response_time) {
            global_stats.min_response_time = response_time;
        }
        if (response_time > global_stats.max_response_time) {
            global_stats.max_response_time = response_time;
        }
    } else {
        global_stats.failed_requests++;
    }
    
    pthread_mutex_unlock(&global_stats.lock);
}

// Worker thread function
void* load_test_worker(void* arg) {
    test_config_t *config = (test_config_t*)arg;
    modbus_t *ctx;
    uint16_t *registers;
    
    // Allocate register buffer
    registers = malloc(config->num_registers * sizeof(uint16_t));
    if (!registers) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }
    
    // Create Modbus context
    ctx = modbus_new_tcp(config->ip_address, config->port);
    if (!ctx) {
        fprintf(stderr, "Failed to create Modbus context\n");
        free(registers);
        return NULL;
    }
    
    // Set timeout
    modbus_set_response_timeout(ctx, 1, 0);
    
    // Connect to server
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        free(registers);
        return NULL;
    }
    
    // Perform requests
    for (int i = 0; i < config->requests_per_thread; i++) {
        double start_time = get_time_ms();
        
        int rc = modbus_read_holding_registers(ctx, 
                                               config->register_address,
                                               config->num_registers,
                                               registers);
        
        double end_time = get_time_ms();
        double response_time = end_time - start_time;
        
        if (rc == config->num_registers) {
            update_stats(1, response_time);
        } else {
            update_stats(0, 0.0);
        }
        
        // Small delay to prevent overwhelming the system (optional)
        // usleep(1000); // 1ms
    }
    
    modbus_close(ctx);
    modbus_free(ctx);
    free(registers);
    
    return NULL;
}

// Run load test
void run_load_test(test_config_t *config) {
    pthread_t *threads;
    double start_time, end_time, duration;
    
    printf("=== Modbus Load Test ===\n");
    printf("Target: %s:%d\n", config->ip_address, config->port);
    printf("Threads: %d\n", config->num_threads);
    printf("Requests per thread: %d\n", config->requests_per_thread);
    printf("Total requests: %d\n", 
           config->num_threads * config->requests_per_thread);
    printf("\nStarting test...\n");
    
    // Allocate thread array
    threads = malloc(config->num_threads * sizeof(pthread_t));
    if (!threads) {
        fprintf(stderr, "Failed to allocate thread array\n");
        return;
    }
    
    // Record start time
    start_time = get_time_ms();
    
    // Create worker threads
    for (int i = 0; i < config->num_threads; i++) {
        if (pthread_create(&threads[i], NULL, load_test_worker, config) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < config->num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Record end time
    end_time = get_time_ms();
    duration = (end_time - start_time) / 1000.0; // Convert to seconds
    
    // Calculate and display results
    unsigned long total_requests = global_stats.successful_requests + 
                                   global_stats.failed_requests;
    double success_rate = (total_requests > 0) ? 
                         (100.0 * global_stats.successful_requests / total_requests) : 0.0;
    double avg_response_time = (global_stats.successful_requests > 0) ?
                               (global_stats.total_response_time / global_stats.successful_requests) : 0.0;
    double throughput = total_requests / duration;
    
    printf("\n=== Test Results ===\n");
    printf("Duration: %.2f seconds\n", duration);
    printf("Total requests: %lu\n", total_requests);
    printf("Successful: %lu\n", global_stats.successful_requests);
    printf("Failed: %lu\n", global_stats.failed_requests);
    printf("Success rate: %.2f%%\n", success_rate);
    printf("Throughput: %.2f requests/sec\n", throughput);
    printf("\nResponse Times:\n");
    printf("  Average: %.2f ms\n", avg_response_time);
    printf("  Minimum: %.2f ms\n", global_stats.min_response_time);
    printf("  Maximum: %.2f ms\n", global_stats.max_response_time);
    
    free(threads);
}

// Stress test with incremental load
void run_stress_test(test_config_t *config) {
    printf("=== Modbus Stress Test ===\n");
    printf("Testing with incremental thread counts...\n\n");
    
    int thread_counts[] = {1, 5, 10, 25, 50, 100, 200};
    int num_levels = sizeof(thread_counts) / sizeof(thread_counts[0]);
    
    for (int i = 0; i < num_levels; i++) {
        // Reset statistics
        pthread_mutex_lock(&global_stats.lock);
        global_stats.successful_requests = 0;
        global_stats.failed_requests = 0;
        global_stats.total_response_time = 0.0;
        global_stats.min_response_time = 999999.0;
        global_stats.max_response_time = 0.0;
        pthread_mutex_unlock(&global_stats.lock);
        
        // Update thread count
        config->num_threads = thread_counts[i];
        
        printf("\n--- Testing with %d threads ---\n", config->num_threads);
        run_load_test(config);
        
        // Check if system is degrading significantly
        unsigned long total = global_stats.successful_requests + 
                             global_stats.failed_requests;
        double error_rate = (total > 0) ? 
                           (100.0 * global_stats.failed_requests / total) : 0.0;
        
        if (error_rate > 50.0) {
            printf("\n!!! High error rate detected (%.2f%%) !!!\n", error_rate);
            printf("System may be at or beyond capacity.\n");
            break;
        }
        
        // Cooldown period
        sleep(2);
    }
}

int main(int argc, char *argv[]) {
    test_config_t config = {
        .ip_address = "127.0.0.1",
        .port = 502,
        .num_threads = 10,
        .requests_per_thread = 100,
        .register_address = 0,
        .num_registers = 10
    };
    
    // Parse command line arguments (simplified)
    if (argc > 1) {
        config.ip_address = argv[1];
    }
    if (argc > 2) {
        config.num_threads = atoi(argv[2]);
    }
    
    // Run load test
    run_load_test(&config);
    
    printf("\n\n");
    
    // Run stress test
    run_stress_test(&config);
    
    return 0;
}
```

### Performance Monitor

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>

// Resource monitoring structure
typedef struct {
    double cpu_usage;
    long memory_usage_kb;
    double start_time;
    double end_time;
} resource_monitor_t;

// Get CPU usage (simplified - Linux specific)
double get_cpu_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    
    double user_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
    double sys_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
    
    return user_time + sys_time;
}

// Get memory usage in KB
long get_memory_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss; // Maximum resident set size in KB
}

// Monitor and log resources during test
void monitor_resources(resource_monitor_t *monitor) {
    monitor->cpu_usage = get_cpu_usage();
    monitor->memory_usage_kb = get_memory_usage();
    
    printf("Resource Usage:\n");
    printf("  CPU Time: %.2f seconds\n", monitor->cpu_usage);
    printf("  Memory: %ld KB\n", monitor->memory_usage_kb);
}
```

## Rust Implementation

```rust
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};
use tokio_modbus::prelude::*;

// Test configuration
#[derive(Clone)]
struct TestConfig {
    host: String,
    port: u16,
    num_threads: usize,
    requests_per_thread: usize,
    register_address: u16,
    num_registers: u16,
}

// Test statistics
#[derive(Default)]
struct TestStats {
    successful_requests: u64,
    failed_requests: u64,
    total_response_time: f64,
    min_response_time: f64,
    max_response_time: f64,
}

impl TestStats {
    fn new() -> Self {
        TestStats {
            successful_requests: 0,
            failed_requests: 0,
            total_response_time: 0.0,
            min_response_time: f64::MAX,
            max_response_time: 0.0,
        }
    }
    
    fn update(&mut self, success: bool, response_time: f64) {
        if success {
            self.successful_requests += 1;
            self.total_response_time += response_time;
            self.min_response_time = self.min_response_time.min(response_time);
            self.max_response_time = self.max_response_time.max(response_time);
        } else {
            self.failed_requests += 1;
        }
    }
    
    fn merge(&mut self, other: &TestStats) {
        self.successful_requests += other.successful_requests;
        self.failed_requests += other.failed_requests;
        self.total_response_time += other.total_response_time;
        self.min_response_time = self.min_response_time.min(other.min_response_time);
        self.max_response_time = self.max_response_time.max(other.max_response_time);
    }
}

// Async worker function
async fn load_test_worker(config: TestConfig) -> TestStats {
    let mut stats = TestStats::new();
    let socket_addr = format!("{}:{}", config.host, config.port);
    
    // Create Modbus client
    let mut ctx = match tcp::connect(socket_addr).await {
        Ok(ctx) => ctx,
        Err(e) => {
            eprintln!("Connection failed: {}", e);
            stats.failed_requests = config.requests_per_thread as u64;
            return stats;
        }
    };
    
    // Perform requests
    for _ in 0..config.requests_per_thread {
        let start = Instant::now();
        
        let result = ctx.read_holding_registers(
            config.register_address,
            config.num_registers,
        ).await;
        
        let duration = start.elapsed();
        let response_time = duration.as_secs_f64() * 1000.0; // Convert to ms
        
        match result {
            Ok(_) => stats.update(true, response_time),
            Err(_) => stats.update(false, 0.0),
        }
    }
    
    stats
}

// Run load test with multiple threads
async fn run_load_test(config: TestConfig) {
    println!("=== Modbus Load Test ===");
    println!("Target: {}:{}", config.host, config.port);
    println!("Threads: {}", config.num_threads);
    println!("Requests per thread: {}", config.requests_per_thread);
    println!("Total requests: {}", config.num_threads * config.requests_per_thread);
    println!("\nStarting test...");
    
    let start_time = Instant::now();
    let stats = Arc::new(Mutex::new(TestStats::new()));
    let mut handles = vec![];
    
    // Spawn worker tasks
    for _ in 0..config.num_threads {
        let config_clone = config.clone();
        let stats_clone = Arc::clone(&stats);
        
        let handle = tokio::spawn(async move {
            let worker_stats = load_test_worker(config_clone).await;
            let mut global_stats = stats_clone.lock().unwrap();
            global_stats.merge(&worker_stats);
        });
        
        handles.push(handle);
    }
    
    // Wait for all workers to complete
    for handle in handles {
        handle.await.unwrap();
    }
    
    let duration = start_time.elapsed();
    let stats = stats.lock().unwrap();
    
    // Calculate and display results
    print_results(&stats, duration);
}

// Print test results
fn print_results(stats: &TestStats, duration: Duration) {
    let total_requests = stats.successful_requests + stats.failed_requests;
    let success_rate = if total_requests > 0 {
        100.0 * stats.successful_requests as f64 / total_requests as f64
    } else {
        0.0
    };
    
    let avg_response_time = if stats.successful_requests > 0 {
        stats.total_response_time / stats.successful_requests as f64
    } else {
        0.0
    };
    
    let throughput = total_requests as f64 / duration.as_secs_f64();
    
    println!("\n=== Test Results ===");
    println!("Duration: {:.2} seconds", duration.as_secs_f64());
    println!("Total requests: {}", total_requests);
    println!("Successful: {}", stats.successful_requests);
    println!("Failed: {}", stats.failed_requests);
    println!("Success rate: {:.2}%", success_rate);
    println!("Throughput: {:.2} requests/sec", throughput);
    println!("\nResponse Times:");
    println!("  Average: {:.2} ms", avg_response_time);
    println!("  Minimum: {:.2} ms", stats.min_response_time);
    println!("  Maximum: {:.2} ms", stats.max_response_time);
}

// Stress test with incremental load
async fn run_stress_test(base_config: TestConfig) {
    println!("=== Modbus Stress Test ===");
    println!("Testing with incremental thread counts...\n");
    
    let thread_counts = vec![1, 5, 10, 25, 50, 100, 200];
    
    for &thread_count in &thread_counts {
        let mut config = base_config.clone();
        config.num_threads = thread_count;
        
        println!("\n--- Testing with {} threads ---", thread_count);
        run_load_test(config).await;
        
        // Cooldown period
        tokio::time::sleep(Duration::from_secs(2)).await;
    }
}

// Performance monitoring
struct PerformanceMonitor {
    start_time: Instant,
    measurements: Vec<(f64, u64)>, // (time, memory)
}

impl PerformanceMonitor {
    fn new() -> Self {
        PerformanceMonitor {
            start_time: Instant::now(),
            measurements: Vec::new(),
        }
    }
    
    fn record(&mut self) {
        let elapsed = self.start_time.elapsed().as_secs_f64();
        
        // Get memory usage (platform-specific)
        #[cfg(target_os = "linux")]
        let memory = Self::get_memory_usage_linux();
        
        #[cfg(not(target_os = "linux"))]
        let memory = 0;
        
        self.measurements.push((elapsed, memory));
    }
    
    #[cfg(target_os = "linux")]
    fn get_memory_usage_linux() -> u64 {
        use std::fs;
        
        if let Ok(contents) = fs::read_to_string("/proc/self/status") {
            for line in contents.lines() {
                if line.starts_with("VmRSS:") {
                    let parts: Vec<&str> = line.split_whitespace().collect();
                    if parts.len() >= 2 {
                        return parts[1].parse().unwrap_or(0);
                    }
                }
            }
        }
        0
    }
    
    fn print_summary(&self) {
        println!("\n=== Performance Summary ===");
        if let Some((_, max_memory)) = self.measurements.iter().max_by_key(|(_, m)| m) {
            println!("Peak memory usage: {} KB", max_memory);
        }
        println!("Total measurements: {}", self.measurements.len());
    }
}

#[tokio::main]
async fn main() {
    let config = TestConfig {
        host: "127.0.0.1".to_string(),
        port: 502,
        num_threads: 10,
        requests_per_thread: 100,
        register_address: 0,
        num_registers: 10,
    };
    
    // Run load test
    run_load_test(config.clone()).await;
    
    println!("\n\n");
    
    // Run stress test
    run_stress_test(config).await;
}
```

### Advanced Rust Testing with Metrics

```rust
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;
use tokio::time::{interval, Duration};

// Detailed metrics collector
struct MetricsCollector {
    latency_buckets: Arc<RwLock<HashMap<String, Vec<f64>>>>,
    error_counts: Arc<RwLock<HashMap<String, u64>>>,
    throughput_samples: Arc<RwLock<Vec<(Instant, u64)>>>,
}

impl MetricsCollector {
    fn new() -> Self {
        MetricsCollector {
            latency_buckets: Arc::new(RwLock::new(HashMap::new())),
            error_counts: Arc::new(RwLock::new(HashMap::new())),
            throughput_samples: Arc::new(RwLock::new(Vec::new())),
        }
    }
    
    async fn record_latency(&self, operation: &str, latency: f64) {
        let mut buckets = self.latency_buckets.write().await;
        buckets.entry(operation.to_string())
               .or_insert_with(Vec::new)
               .push(latency);
    }
    
    async fn record_error(&self, error_type: &str) {
        let mut errors = self.error_counts.write().await;
        *errors.entry(error_type.to_string()).or_insert(0) += 1;
    }
    
    async fn calculate_percentiles(&self, operation: &str) -> Option<(f64, f64, f64)> {
        let buckets = self.latency_buckets.read().await;
        if let Some(latencies) = buckets.get(operation) {
            let mut sorted = latencies.clone();
            sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
            
            let len = sorted.len();
            if len == 0 {
                return None;
            }
            
            let p50 = sorted[len / 2];
            let p95 = sorted[(len * 95) / 100];
            let p99 = sorted[(len * 99) / 100];
            
            Some((p50, p95, p99))
        } else {
            None
        }
    }
    
    async fn print_report(&self) {
        println!("\n=== Detailed Metrics Report ===");
        
        // Print latency percentiles
        let buckets = self.latency_buckets.read().await;
        for (operation, _) in buckets.iter() {
            if let Some((p50, p95, p99)) = self.calculate_percentiles(operation).await {
                println!("\n{} Latency:", operation);
                println!("  P50: {:.2} ms", p50);
                println!("  P95: {:.2} ms", p95);
                println!("  P99: {:.2} ms", p99);
            }
        }
        
        // Print error counts
        let errors = self.error_counts.read().await;
        if !errors.is_empty() {
            println!("\nErrors:");
            for (error_type, count) in errors.iter() {
                println!("  {}: {}", error_type, count);
            }
        }
    }
}
```

## Summary

Load testing and stress testing are essential practices for ensuring Modbus systems can handle production workloads reliably. The key aspects include:

**Load Testing Objectives:**
- Validate system performance under expected operational conditions
- Measure throughput, latency, and resource utilization
- Identify optimal configuration parameters
- Ensure stable operation under sustained load

**Stress Testing Objectives:**
- Determine system breaking points and maximum capacity
- Identify how the system degrades under extreme load
- Validate recovery mechanisms and error handling
- Discover resource leaks and bottlenecks

**Implementation Considerations:**
- Use multi-threaded/async approaches to simulate concurrent clients
- Measure both successful transactions and error rates
- Monitor system resources (CPU, memory, network) during tests
- Incrementally increase load to find capacity limits
- Allow cooldown periods between test iterations
- Record detailed metrics for analysis

**Best Practices:**
- Test in an environment that closely mirrors production
- Establish baseline performance metrics before optimization
- Test various scenarios (read vs. write, different register counts)
- Include both short bursts and sustained load tests
- Document results and track performance over time
- Use the data to set appropriate connection limits and timeouts

The provided implementations demonstrate comprehensive testing frameworks in both C/C++ and Rust, with features for concurrent testing, statistical analysis, resource monitoring, and incremental stress testing to thoroughly validate Modbus system performance.