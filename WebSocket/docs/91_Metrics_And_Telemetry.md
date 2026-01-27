# WebSocket Metrics and Telemetry

## Overview

Metrics and telemetry for WebSocket connections involve collecting, tracking, and analyzing operational data to monitor performance, diagnose issues, and optimize system behavior. This includes measuring connection health, message throughput, latency, error rates, and resource utilization.

## Key Metrics Categories

### Connection Metrics
- **Active connections**: Current number of open WebSocket connections
- **Connection lifetime**: Duration of each connection
- **Connection churn**: Rate of connections opening/closing
- **Failed connection attempts**: Authentication failures, protocol errors

### Message Metrics
- **Message rate**: Messages per second (sent/received)
- **Message size distribution**: Tracking payload sizes
- **Message queue depth**: Backlog of pending messages
- **Throughput**: Total bytes transferred over time

### Latency Metrics
- **Round-trip time (RTT)**: Time for ping-pong exchanges
- **Message processing latency**: Time from receive to processing completion
- **Queue wait time**: Time messages spend in queues
- **End-to-end latency**: Client to server to client response time

### Error Metrics
- **Error rates**: Failed sends, protocol violations
- **Timeout counts**: Connection or operation timeouts
- **Abnormal closures**: Unexpected disconnections

---

## C/C++ Implementation

Here's a comprehensive metrics collection system using modern C++:

```cpp
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <thread>
#include <cmath>

using namespace std::chrono;

// Metric aggregator for statistical calculations
class MetricAggregator {
private:
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> sum_{0};
    std::atomic<uint64_t> min_{UINT64_MAX};
    std::atomic<uint64_t> max_{0};
    std::mutex histogram_mutex_;
    std::vector<uint64_t> samples_;  // For percentile calculation
    
public:
    void record(uint64_t value) {
        count_.fetch_add(1, std::memory_order_relaxed);
        sum_.fetch_add(value, std::memory_order_relaxed);
        
        // Update min
        uint64_t current_min = min_.load(std::memory_order_relaxed);
        while (value < current_min && 
               !min_.compare_exchange_weak(current_min, value, std::memory_order_relaxed));
        
        // Update max
        uint64_t current_max = max_.load(std::memory_order_relaxed);
        while (value > current_max && 
               !max_.compare_exchange_weak(current_max, value, std::memory_order_relaxed));
        
        // Store sample for percentile calculation
        std::lock_guard<std::mutex> lock(histogram_mutex_);
        samples_.push_back(value);
    }
    
    double average() const {
        uint64_t cnt = count_.load(std::memory_order_relaxed);
        return cnt > 0 ? static_cast<double>(sum_.load()) / cnt : 0.0;
    }
    
    uint64_t minimum() const { return min_.load(std::memory_order_relaxed); }
    uint64_t maximum() const { return max_.load(std::memory_order_relaxed); }
    uint64_t total() const { return count_.load(std::memory_order_relaxed); }
    
    double percentile(double p) {
        std::lock_guard<std::mutex> lock(histogram_mutex_);
        if (samples_.empty()) return 0.0;
        
        auto samples_copy = samples_;
        std::sort(samples_copy.begin(), samples_copy.end());
        
        size_t index = static_cast<size_t>(p * samples_copy.size());
        if (index >= samples_copy.size()) index = samples_copy.size() - 1;
        
        return static_cast<double>(samples_copy[index]);
    }
    
    void reset() {
        count_.store(0);
        sum_.store(0);
        min_.store(UINT64_MAX);
        max_.store(0);
        std::lock_guard<std::mutex> lock(histogram_mutex_);
        samples_.clear();
    }
};

// WebSocket Telemetry System
class WebSocketTelemetry {
private:
    // Connection metrics
    std::atomic<uint64_t> active_connections_{0};
    std::atomic<uint64_t> total_connections_{0};
    std::atomic<uint64_t> failed_connections_{0};
    
    // Message metrics
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};
    
    // Latency tracking
    MetricAggregator message_latency_;
    MetricAggregator ping_rtt_;
    MetricAggregator processing_time_;
    
    // Error tracking
    std::atomic<uint64_t> send_errors_{0};
    std::atomic<uint64_t> receive_errors_{0};
    std::atomic<uint64_t> timeouts_{0};
    
    // Timing
    steady_clock::time_point start_time_;
    
public:
    WebSocketTelemetry() : start_time_(steady_clock::now()) {}
    
    // Connection tracking
    void recordConnectionOpen() {
        active_connections_.fetch_add(1, std::memory_order_relaxed);
        total_connections_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void recordConnectionClose() {
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    void recordConnectionFailed() {
        failed_connections_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Message tracking
    void recordMessageSent(size_t bytes) {
        messages_sent_.fetch_add(1, std::memory_order_relaxed);
        bytes_sent_.fetch_add(bytes, std::memory_order_relaxed);
    }
    
    void recordMessageReceived(size_t bytes) {
        messages_received_.fetch_add(1, std::memory_order_relaxed);
        bytes_received_.fetch_add(bytes, std::memory_order_relaxed);
    }
    
    // Latency tracking (time in microseconds)
    void recordMessageLatency(uint64_t latency_us) {
        message_latency_.record(latency_us);
    }
    
    void recordPingRTT(uint64_t rtt_us) {
        ping_rtt_.record(rtt_us);
    }
    
    void recordProcessingTime(uint64_t time_us) {
        processing_time_.record(time_us);
    }
    
    // Error tracking
    void recordSendError() {
        send_errors_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void recordReceiveError() {
        receive_errors_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void recordTimeout() {
        timeouts_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Report generation
    void printReport() const {
        auto now = steady_clock::now();
        auto uptime_sec = duration_cast<seconds>(now - start_time_).count();
        
        std::cout << "=== WebSocket Telemetry Report ===\n";
        std::cout << "Uptime: " << uptime_sec << " seconds\n\n";
        
        std::cout << "Connection Metrics:\n";
        std::cout << "  Active: " << active_connections_.load() << "\n";
        std::cout << "  Total: " << total_connections_.load() << "\n";
        std::cout << "  Failed: " << failed_connections_.load() << "\n\n";
        
        std::cout << "Message Metrics:\n";
        std::cout << "  Sent: " << messages_sent_.load() 
                  << " (" << bytes_sent_.load() / 1024.0 << " KB)\n";
        std::cout << "  Received: " << messages_received_.load() 
                  << " (" << bytes_received_.load() / 1024.0 << " KB)\n";
        
        if (uptime_sec > 0) {
            std::cout << "  Send Rate: " 
                      << messages_sent_.load() / uptime_sec << " msg/s\n";
            std::cout << "  Receive Rate: " 
                      << messages_received_.load() / uptime_sec << " msg/s\n";
        }
        std::cout << "\n";
        
        std::cout << "Latency Metrics (microseconds):\n";
        std::cout << "  Message Latency - Avg: " << message_latency_.average()
                  << ", P50: " << message_latency_.percentile(0.5)
                  << ", P95: " << message_latency_.percentile(0.95)
                  << ", P99: " << message_latency_.percentile(0.99) << "\n";
        std::cout << "  Ping RTT - Avg: " << ping_rtt_.average()
                  << ", Min: " << ping_rtt_.minimum()
                  << ", Max: " << ping_rtt_.maximum() << "\n";
        std::cout << "  Processing Time - Avg: " << processing_time_.average() << "\n\n";
        
        std::cout << "Error Metrics:\n";
        std::cout << "  Send Errors: " << send_errors_.load() << "\n";
        std::cout << "  Receive Errors: " << receive_errors_.load() << "\n";
        std::cout << "  Timeouts: " << timeouts_.load() << "\n";
    }
};

// Example usage in a WebSocket handler
class WebSocketConnection {
private:
    WebSocketTelemetry& telemetry_;
    steady_clock::time_point message_send_time_;
    
public:
    WebSocketConnection(WebSocketTelemetry& telemetry) 
        : telemetry_(telemetry) {
        telemetry_.recordConnectionOpen();
    }
    
    ~WebSocketConnection() {
        telemetry_.recordConnectionClose();
    }
    
    void sendMessage(const std::string& message) {
        message_send_time_ = steady_clock::now();
        
        // Simulate send operation
        bool success = true; // actual send logic here
        
        if (success) {
            telemetry_.recordMessageSent(message.size());
        } else {
            telemetry_.recordSendError();
        }
    }
    
    void onMessageReceived(const std::string& message) {
        auto receive_time = steady_clock::now();
        
        telemetry_.recordMessageReceived(message.size());
        
        // Process message and track processing time
        auto process_start = steady_clock::now();
        processMessage(message);
        auto process_end = steady_clock::now();
        
        auto processing_us = duration_cast<microseconds>(
            process_end - process_start).count();
        telemetry_.recordProcessingTime(processing_us);
    }
    
    void onPongReceived() {
        auto now = steady_clock::now();
        auto rtt_us = duration_cast<microseconds>(
            now - message_send_time_).count();
        telemetry_.recordPingRTT(rtt_us);
    }
    
private:
    void processMessage(const std::string& message) {
        // Message processing logic
        std::this_thread::sleep_for(microseconds(100));
    }
};

// Main example
int main() {
    WebSocketTelemetry telemetry;
    
    // Simulate some WebSocket activity
    {
        WebSocketConnection conn(telemetry);
        
        for (int i = 0; i < 100; ++i) {
            conn.sendMessage("Hello, WebSocket!");
            conn.onMessageReceived("Response message");
            conn.onPongReceived();
            
            std::this_thread::sleep_for(milliseconds(10));
        }
    }
    
    // Print telemetry report
    telemetry.printReport();
    
    return 0;
}
```

---

## Rust Implementation

Here's a robust Rust implementation using async/await and proper type safety:

```rust
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::RwLock;

/// Statistical aggregator for metric values
#[derive(Default)]
struct MetricAggregator {
    count: AtomicU64,
    sum: AtomicU64,
    min: AtomicU64,
    max: AtomicU64,
    samples: Mutex<Vec<u64>>,
}

impl MetricAggregator {
    fn new() -> Self {
        Self {
            count: AtomicU64::new(0),
            sum: AtomicU64::new(0),
            min: AtomicU64::new(u64::MAX),
            max: AtomicU64::new(0),
            samples: Mutex::new(Vec::new()),
        }
    }
    
    fn record(&self, value: u64) {
        self.count.fetch_add(1, Ordering::Relaxed);
        self.sum.fetch_add(value, Ordering::Relaxed);
        
        // Update min
        self.min.fetch_min(value, Ordering::Relaxed);
        
        // Update max
        self.max.fetch_max(value, Ordering::Relaxed);
        
        // Store sample
        if let Ok(mut samples) = self.samples.lock() {
            samples.push(value);
        }
    }
    
    fn average(&self) -> f64 {
        let count = self.count.load(Ordering::Relaxed);
        if count > 0 {
            self.sum.load(Ordering::Relaxed) as f64 / count as f64
        } else {
            0.0
        }
    }
    
    fn minimum(&self) -> u64 {
        self.min.load(Ordering::Relaxed)
    }
    
    fn maximum(&self) -> u64 {
        self.max.load(Ordering::Relaxed)
    }
    
    fn count(&self) -> u64 {
        self.count.load(Ordering::Relaxed)
    }
    
    fn percentile(&self, p: f64) -> f64 {
        if let Ok(mut samples) = self.samples.lock() {
            if samples.is_empty() {
                return 0.0;
            }
            
            samples.sort_unstable();
            let index = ((p * samples.len() as f64) as usize).min(samples.len() - 1);
            samples[index] as f64
        } else {
            0.0
        }
    }
}

/// Comprehensive WebSocket telemetry system
pub struct WebSocketTelemetry {
    // Connection metrics
    active_connections: AtomicUsize,
    total_connections: AtomicU64,
    failed_connections: AtomicU64,
    
    // Message metrics
    messages_sent: AtomicU64,
    messages_received: AtomicU64,
    bytes_sent: AtomicU64,
    bytes_received: AtomicU64,
    
    // Latency metrics
    message_latency: MetricAggregator,
    ping_rtt: MetricAggregator,
    processing_time: MetricAggregator,
    
    // Error metrics
    send_errors: AtomicU64,
    receive_errors: AtomicU64,
    timeouts: AtomicU64,
    
    // Timing
    start_time: Instant,
}

impl WebSocketTelemetry {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            active_connections: AtomicUsize::new(0),
            total_connections: AtomicU64::new(0),
            failed_connections: AtomicU64::new(0),
            messages_sent: AtomicU64::new(0),
            messages_received: AtomicU64::new(0),
            bytes_sent: AtomicU64::new(0),
            bytes_received: AtomicU64::new(0),
            message_latency: MetricAggregator::new(),
            ping_rtt: MetricAggregator::new(),
            processing_time: MetricAggregator::new(),
            send_errors: AtomicU64::new(0),
            receive_errors: AtomicU64::new(0),
            timeouts: AtomicU64::new(0),
            start_time: Instant::now(),
        })
    }
    
    // Connection tracking
    pub fn record_connection_open(&self) {
        self.active_connections.fetch_add(1, Ordering::Relaxed);
        self.total_connections.fetch_add(1, Ordering::Relaxed);
    }
    
    pub fn record_connection_close(&self) {
        self.active_connections.fetch_sub(1, Ordering::Relaxed);
    }
    
    pub fn record_connection_failed(&self) {
        self.failed_connections.fetch_add(1, Ordering::Relaxed);
    }
    
    // Message tracking
    pub fn record_message_sent(&self, bytes: usize) {
        self.messages_sent.fetch_add(1, Ordering::Relaxed);
        self.bytes_sent.fetch_add(bytes as u64, Ordering::Relaxed);
    }
    
    pub fn record_message_received(&self, bytes: usize) {
        self.messages_received.fetch_add(1, Ordering::Relaxed);
        self.bytes_received.fetch_add(bytes as u64, Ordering::Relaxed);
    }
    
    // Latency tracking (microseconds)
    pub fn record_message_latency(&self, latency: Duration) {
        self.message_latency.record(latency.as_micros() as u64);
    }
    
    pub fn record_ping_rtt(&self, rtt: Duration) {
        self.ping_rtt.record(rtt.as_micros() as u64);
    }
    
    pub fn record_processing_time(&self, time: Duration) {
        self.processing_time.record(time.as_micros() as u64);
    }
    
    // Error tracking
    pub fn record_send_error(&self) {
        self.send_errors.fetch_add(1, Ordering::Relaxed);
    }
    
    pub fn record_receive_error(&self) {
        self.receive_errors.fetch_add(1, Ordering::Relaxed);
    }
    
    pub fn record_timeout(&self) {
        self.timeouts.fetch_add(1, Ordering::Relaxed);
    }
    
    // Reporting
    pub fn generate_report(&self) -> TelemetryReport {
        let uptime = self.start_time.elapsed();
        
        TelemetryReport {
            uptime_secs: uptime.as_secs(),
            active_connections: self.active_connections.load(Ordering::Relaxed),
            total_connections: self.total_connections.load(Ordering::Relaxed),
            failed_connections: self.failed_connections.load(Ordering::Relaxed),
            messages_sent: self.messages_sent.load(Ordering::Relaxed),
            messages_received: self.messages_received.load(Ordering::Relaxed),
            bytes_sent: self.bytes_sent.load(Ordering::Relaxed),
            bytes_received: self.bytes_received.load(Ordering::Relaxed),
            message_latency_avg: self.message_latency.average(),
            message_latency_p50: self.message_latency.percentile(0.5),
            message_latency_p95: self.message_latency.percentile(0.95),
            message_latency_p99: self.message_latency.percentile(0.99),
            ping_rtt_avg: self.ping_rtt.average(),
            ping_rtt_min: self.ping_rtt.minimum(),
            ping_rtt_max: self.ping_rtt.maximum(),
            processing_time_avg: self.processing_time.average(),
            send_errors: self.send_errors.load(Ordering::Relaxed),
            receive_errors: self.receive_errors.load(Ordering::Relaxed),
            timeouts: self.timeouts.load(Ordering::Relaxed),
        }
    }
    
    pub fn print_report(&self) {
        let report = self.generate_report();
        println!("{}", report);
    }
}

/// Telemetry report structure
#[derive(Debug)]
pub struct TelemetryReport {
    pub uptime_secs: u64,
    pub active_connections: usize,
    pub total_connections: u64,
    pub failed_connections: u64,
    pub messages_sent: u64,
    pub messages_received: u64,
    pub bytes_sent: u64,
    pub bytes_received: u64,
    pub message_latency_avg: f64,
    pub message_latency_p50: f64,
    pub message_latency_p95: f64,
    pub message_latency_p99: f64,
    pub ping_rtt_avg: f64,
    pub ping_rtt_min: u64,
    pub ping_rtt_max: u64,
    pub processing_time_avg: f64,
    pub send_errors: u64,
    pub receive_errors: u64,
    pub timeouts: u64,
}

impl std::fmt::Display for TelemetryReport {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "=== WebSocket Telemetry Report ===")?;
        writeln!(f, "Uptime: {} seconds\n", self.uptime_secs)?;
        
        writeln!(f, "Connection Metrics:")?;
        writeln!(f, "  Active: {}", self.active_connections)?;
        writeln!(f, "  Total: {}", self.total_connections)?;
        writeln!(f, "  Failed: {}\n", self.failed_connections)?;
        
        writeln!(f, "Message Metrics:")?;
        writeln!(f, "  Sent: {} ({:.2} KB)", 
                 self.messages_sent, self.bytes_sent as f64 / 1024.0)?;
        writeln!(f, "  Received: {} ({:.2} KB)", 
                 self.messages_received, self.bytes_received as f64 / 1024.0)?;
        
        if self.uptime_secs > 0 {
            writeln!(f, "  Send Rate: {:.2} msg/s", 
                     self.messages_sent as f64 / self.uptime_secs as f64)?;
            writeln!(f, "  Receive Rate: {:.2} msg/s", 
                     self.messages_received as f64 / self.uptime_secs as f64)?;
        }
        writeln!(f)?;
        
        writeln!(f, "Latency Metrics (microseconds):")?;
        writeln!(f, "  Message Latency - Avg: {:.2}, P50: {:.2}, P95: {:.2}, P99: {:.2}",
                 self.message_latency_avg, self.message_latency_p50,
                 self.message_latency_p95, self.message_latency_p99)?;
        writeln!(f, "  Ping RTT - Avg: {:.2}, Min: {}, Max: {}",
                 self.ping_rtt_avg, self.ping_rtt_min, self.ping_rtt_max)?;
        writeln!(f, "  Processing Time - Avg: {:.2}\n", self.processing_time_avg)?;
        
        writeln!(f, "Error Metrics:")?;
        writeln!(f, "  Send Errors: {}", self.send_errors)?;
        writeln!(f, "  Receive Errors: {}", self.receive_errors)?;
        writeln!(f, "  Timeouts: {}", self.timeouts)?;
        
        Ok(())
    }
}

/// Example WebSocket connection with telemetry
pub struct WebSocketConnection {
    telemetry: Arc<WebSocketTelemetry>,
    last_ping_time: RwLock<Option<Instant>>,
}

impl WebSocketConnection {
    pub fn new(telemetry: Arc<WebSocketTelemetry>) -> Self {
        telemetry.record_connection_open();
        Self {
            telemetry,
            last_ping_time: RwLock::new(None),
        }
    }
    
    pub async fn send_message(&self, message: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        // Simulate send operation
        let result = self.simulate_send(message).await;
        
        match result {
            Ok(_) => {
                self.telemetry.record_message_sent(message.len());
                Ok(())
            }
            Err(e) => {
                self.telemetry.record_send_error();
                Err(e)
            }
        }
    }
    
    pub async fn on_message_received(&self, message: &[u8]) {
        let process_start = Instant::now();
        
        self.telemetry.record_message_received(message.len());
        
        // Process the message
        self.process_message(message).await;
        
        let processing_time = process_start.elapsed();
        self.telemetry.record_processing_time(processing_time);
    }
    
    pub async fn send_ping(&self) {
        let mut last_ping = self.last_ping_time.write().await;
        *last_ping = Some(Instant::now());
    }
    
    pub async fn on_pong_received(&self) {
        if let Some(ping_time) = *self.last_ping_time.read().await {
            let rtt = ping_time.elapsed();
            self.telemetry.record_ping_rtt(rtt);
        }
    }
    
    async fn simulate_send(&self, _message: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        tokio::time::sleep(Duration::from_micros(50)).await;
        Ok(())
    }
    
    async fn process_message(&self, _message: &[u8]) {
        tokio::time::sleep(Duration::from_micros(100)).await;
    }
}

impl Drop for WebSocketConnection {
    fn drop(&mut self) {
        self.telemetry.record_connection_close();
    }
}

// Example usage
#[tokio::main]
async fn main() {
    let telemetry = WebSocketTelemetry::new();
    
    // Simulate WebSocket activity
    {
        let conn = WebSocketConnection::new(telemetry.clone());
        
        for _ in 0..100 {
            let _ = conn.send_message(b"Hello, WebSocket!").await;
            conn.on_message_received(b"Response message").await;
            
            conn.send_ping().await;
            tokio::time::sleep(Duration::from_micros(500)).await;
            conn.on_pong_received().await;
            
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }
    
    // Print telemetry report
    telemetry.print_report();
}
```

---

## Summary

**WebSocket Metrics and Telemetry** is essential for maintaining reliable, performant real-time communication systems. Key takeaways:

- **Connection metrics** track active sessions, connection churn, and failure rates to monitor system health
- **Message metrics** measure throughput (messages/sec, bytes/sec) and help identify bottlenecks
- **Latency metrics** including RTT, processing time, and end-to-end latency reveal performance characteristics
- **Error tracking** helps identify patterns in failures, timeouts, and protocol violations

Both implementations demonstrate thread-safe metric collection using atomic operations, statistical aggregation with percentile calculations (P50, P95, P99), and comprehensive reporting. The C++ version uses `std::atomic` and mutexes, while the Rust version leverages its ownership system and async runtime for safety and performance.

Effective telemetry enables proactive monitoring, capacity planning, performance optimization, and rapid incident response in production WebSocket systems.