# WebSocket Logging and Monitoring: A Comprehensive Guide

Logging and monitoring are critical components of production WebSocket systems. Unlike traditional HTTP services, WebSocket connections are long-lived and stateful, requiring specialized approaches to observability. Effective logging and monitoring help you track connection lifecycles, debug issues, measure performance, and ensure system reliability.

## Core Concepts

**Structured Logging** organizes log data in a consistent, machine-parsable format (typically JSON) rather than unstructured text. This enables powerful querying, filtering, and analysis.

**Metrics Collection** involves gathering quantitative measurements about system behavior - connection counts, message rates, latency percentiles, error rates, etc.

**Observability** is the broader practice of understanding system internal states through logs, metrics, and traces, enabling you to answer arbitrary questions about system behavior.

## Key Metrics for WebSocket Systems

1. **Connection Metrics**: Active connections, connection rate, disconnection rate, connection duration
2. **Message Metrics**: Messages sent/received per second, message size distribution, queue depths
3. **Performance Metrics**: Message latency (end-to-end), processing time, network RTT
4. **Error Metrics**: Connection failures, authentication failures, protocol errors, application errors
5. **Resource Metrics**: Memory usage per connection, CPU utilization, bandwidth consumption

## C Implementation

Here's a comprehensive logging and monitoring system in C:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

// Log levels
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4
} LogLevel;

// Metrics structure
typedef struct {
    // Connection metrics
    long active_connections;
    long total_connections;
    long total_disconnections;
    
    // Message metrics
    long messages_sent;
    long messages_received;
    long bytes_sent;
    long bytes_received;
    
    // Error metrics
    long connection_errors;
    long protocol_errors;
    long application_errors;
    
    // Performance metrics
    double avg_latency_ms;
    double max_latency_ms;
    long latency_samples;
    
    pthread_mutex_t lock;
} WebSocketMetrics;

// Logger structure
typedef struct {
    FILE* file;
    LogLevel min_level;
    int structured; // 0 = plain text, 1 = JSON
    pthread_mutex_t lock;
} Logger;

// Global logger and metrics
static Logger* global_logger = NULL;
static WebSocketMetrics* global_metrics = NULL;

// Get current timestamp in ISO 8601 format
void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", tm_info);
    snprintf(buffer + strlen(buffer), size - strlen(buffer), 
             ".%03ldZ", tv.tv_usec / 1000);
}

// Convert log level to string
const char* log_level_string(LogLevel level) {
    switch(level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_FATAL: return "FATAL";
        default:        return "UNKNOWN";
    }
}

// Initialize logger
Logger* logger_create(const char* filename, LogLevel min_level, int structured) {
    Logger* logger = (Logger*)malloc(sizeof(Logger));
    if (!logger) return NULL;
    
    logger->file = filename ? fopen(filename, "a") : stdout;
    logger->min_level = min_level;
    logger->structured = structured;
    pthread_mutex_init(&logger->lock, NULL);
    
    return logger;
}

// Structured logging (JSON format)
void log_structured(Logger* logger, LogLevel level, const char* component,
                   const char* event, const char* message, ...) {
    if (level < logger->min_level) return;
    
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    pthread_mutex_lock(&logger->lock);
    
    fprintf(logger->file, "{");
    fprintf(logger->file, "\"timestamp\":\"%s\",", timestamp);
    fprintf(logger->file, "\"level\":\"%s\",", log_level_string(level));
    fprintf(logger->file, "\"component\":\"%s\",", component);
    fprintf(logger->file, "\"event\":\"%s\",", event);
    
    // Format message
    char msg_buffer[1024];
    va_list args;
    va_start(args, message);
    vsnprintf(msg_buffer, sizeof(msg_buffer), message, args);
    va_end(args);
    
    fprintf(logger->file, "\"message\":\"%s\"", msg_buffer);
    fprintf(logger->file, "}\n");
    fflush(logger->file);
    
    pthread_mutex_unlock(&logger->lock);
}

// Plain text logging
void log_plain(Logger* logger, LogLevel level, const char* format, ...) {
    if (level < logger->min_level) return;
    
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    pthread_mutex_lock(&logger->lock);
    
    fprintf(logger->file, "[%s] [%s] ", timestamp, log_level_string(level));
    
    va_list args;
    va_start(args, format);
    vfprintf(logger->file, format, args);
    va_end(args);
    
    fprintf(logger->file, "\n");
    fflush(logger->file);
    
    pthread_mutex_unlock(&logger->lock);
}

// Initialize metrics
WebSocketMetrics* metrics_create() {
    WebSocketMetrics* metrics = (WebSocketMetrics*)calloc(1, sizeof(WebSocketMetrics));
    if (!metrics) return NULL;
    
    pthread_mutex_init(&metrics->lock, NULL);
    return metrics;
}

// Record connection event
void metrics_connection_opened(WebSocketMetrics* metrics) {
    pthread_mutex_lock(&metrics->lock);
    metrics->active_connections++;
    metrics->total_connections++;
    pthread_mutex_unlock(&metrics->lock);
}

void metrics_connection_closed(WebSocketMetrics* metrics) {
    pthread_mutex_lock(&metrics->lock);
    metrics->active_connections--;
    metrics->total_disconnections++;
    pthread_mutex_unlock(&metrics->lock);
}

// Record message event
void metrics_message_sent(WebSocketMetrics* metrics, size_t bytes) {
    pthread_mutex_lock(&metrics->lock);
    metrics->messages_sent++;
    metrics->bytes_sent += bytes;
    pthread_mutex_unlock(&metrics->lock);
}

void metrics_message_received(WebSocketMetrics* metrics, size_t bytes) {
    pthread_mutex_lock(&metrics->lock);
    metrics->messages_received++;
    metrics->bytes_received += bytes;
    pthread_mutex_unlock(&metrics->lock);
}

// Record latency
void metrics_record_latency(WebSocketMetrics* metrics, double latency_ms) {
    pthread_mutex_lock(&metrics->lock);
    
    // Running average calculation
    double total = metrics->avg_latency_ms * metrics->latency_samples;
    metrics->latency_samples++;
    metrics->avg_latency_ms = (total + latency_ms) / metrics->latency_samples;
    
    if (latency_ms > metrics->max_latency_ms) {
        metrics->max_latency_ms = latency_ms;
    }
    
    pthread_mutex_unlock(&metrics->lock);
}

// Record errors
void metrics_record_error(WebSocketMetrics* metrics, const char* error_type) {
    pthread_mutex_lock(&metrics->lock);
    
    if (strcmp(error_type, "connection") == 0) {
        metrics->connection_errors++;
    } else if (strcmp(error_type, "protocol") == 0) {
        metrics->protocol_errors++;
    } else if (strcmp(error_type, "application") == 0) {
        metrics->application_errors++;
    }
    
    pthread_mutex_unlock(&metrics->lock);
}

// Export metrics in Prometheus format
void metrics_export_prometheus(WebSocketMetrics* metrics, FILE* output) {
    pthread_mutex_lock(&metrics->lock);
    
    fprintf(output, "# HELP websocket_active_connections Current active connections\n");
    fprintf(output, "# TYPE websocket_active_connections gauge\n");
    fprintf(output, "websocket_active_connections %ld\n\n", metrics->active_connections);
    
    fprintf(output, "# HELP websocket_total_connections Total connections opened\n");
    fprintf(output, "# TYPE websocket_total_connections counter\n");
    fprintf(output, "websocket_total_connections %ld\n\n", metrics->total_connections);
    
    fprintf(output, "# HELP websocket_messages_sent_total Total messages sent\n");
    fprintf(output, "# TYPE websocket_messages_sent_total counter\n");
    fprintf(output, "websocket_messages_sent_total %ld\n\n", metrics->messages_sent);
    
    fprintf(output, "# HELP websocket_messages_received_total Total messages received\n");
    fprintf(output, "# TYPE websocket_messages_received_total counter\n");
    fprintf(output, "websocket_messages_received_total %ld\n\n", metrics->messages_received);
    
    fprintf(output, "# HELP websocket_latency_avg_ms Average message latency\n");
    fprintf(output, "# TYPE websocket_latency_avg_ms gauge\n");
    fprintf(output, "websocket_latency_avg_ms %.2f\n\n", metrics->avg_latency_ms);
    
    fprintf(output, "# HELP websocket_errors_total Total errors by type\n");
    fprintf(output, "# TYPE websocket_errors_total counter\n");
    fprintf(output, "websocket_errors_total{type=\"connection\"} %ld\n", 
            metrics->connection_errors);
    fprintf(output, "websocket_errors_total{type=\"protocol\"} %ld\n", 
            metrics->protocol_errors);
    fprintf(output, "websocket_errors_total{type=\"application\"} %ld\n", 
            metrics->application_errors);
    
    pthread_mutex_unlock(&metrics->lock);
}

// Example usage
int main() {
    // Initialize logging
    global_logger = logger_create("websocket.log", LOG_INFO, 1);
    global_metrics = metrics_create();
    
    // Simulate WebSocket events
    log_structured(global_logger, LOG_INFO, "server", "startup", 
                   "WebSocket server starting on port %d", 8080);
    
    // Connection opened
    metrics_connection_opened(global_metrics);
    log_structured(global_logger, LOG_INFO, "connection", "opened",
                   "Client connected from %s", "192.168.1.100");
    
    // Message received
    metrics_message_received(global_metrics, 256);
    metrics_record_latency(global_metrics, 15.5);
    log_structured(global_logger, LOG_DEBUG, "message", "received",
                   "Received message of size %d bytes", 256);
    
    // Error occurred
    metrics_record_error(global_metrics, "protocol");
    log_structured(global_logger, LOG_ERROR, "protocol", "error",
                   "Invalid frame opcode: %d", 0x0F);
    
    // Export metrics
    FILE* metrics_file = fopen("metrics.txt", "w");
    metrics_export_prometheus(global_metrics, metrics_file);
    fclose(metrics_file);
    
    // Cleanup
    if (global_logger->file != stdout) {
        fclose(global_logger->file);
    }
    pthread_mutex_destroy(&global_logger->lock);
    pthread_mutex_destroy(&global_metrics->lock);
    free(global_logger);
    free(global_metrics);
    
    return 0;
}
```

## Rust Implementation

Here's a production-ready logging and monitoring system in Rust using popular crates:

```rust
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::atomic::{AtomicI64, AtomicU64, Ordering};
use std::sync::{Arc, RwLock};
use std::time::{Duration, Instant};
use tracing::{debug, error, info, warn, Level};
use tracing_subscriber::{fmt, layer::SubscriberExt, util::SubscriberInitExt, EnvFilter};

// Structured log event
#[derive(Debug, Serialize, Deserialize)]
struct LogEvent {
    timestamp: DateTime<Utc>,
    level: String,
    component: String,
    event: String,
    message: String,
    #[serde(flatten)]
    fields: HashMap<String, serde_json::Value>,
}

// WebSocket metrics collector
#[derive(Debug, Clone)]
pub struct WebSocketMetrics {
    // Connection metrics
    active_connections: Arc<AtomicI64>,
    total_connections: Arc<AtomicU64>,
    total_disconnections: Arc<AtomicU64>,
    
    // Message metrics
    messages_sent: Arc<AtomicU64>,
    messages_received: Arc<AtomicU64>,
    bytes_sent: Arc<AtomicU64>,
    bytes_received: Arc<AtomicU64>,
    
    // Error metrics
    connection_errors: Arc<AtomicU64>,
    protocol_errors: Arc<AtomicU64>,
    application_errors: Arc<AtomicU64>,
    
    // Latency tracking
    latency_histogram: Arc<RwLock<LatencyHistogram>>,
}

#[derive(Debug)]
struct LatencyHistogram {
    samples: Vec<f64>,
    max_samples: usize,
}

impl LatencyHistogram {
    fn new(max_samples: usize) -> Self {
        Self {
            samples: Vec::with_capacity(max_samples),
            max_samples,
        }
    }
    
    fn record(&mut self, latency_ms: f64) {
        if self.samples.len() >= self.max_samples {
            self.samples.remove(0);
        }
        self.samples.push(latency_ms);
    }
    
    fn percentile(&self, p: f64) -> Option<f64> {
        if self.samples.is_empty() {
            return None;
        }
        
        let mut sorted = self.samples.clone();
        sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
        
        let index = ((p / 100.0) * (sorted.len() - 1) as f64) as usize;
        Some(sorted[index])
    }
    
    fn average(&self) -> Option<f64> {
        if self.samples.is_empty() {
            return None;
        }
        Some(self.samples.iter().sum::<f64>() / self.samples.len() as f64)
    }
}

impl WebSocketMetrics {
    pub fn new() -> Self {
        Self {
            active_connections: Arc::new(AtomicI64::new(0)),
            total_connections: Arc::new(AtomicU64::new(0)),
            total_disconnections: Arc::new(AtomicU64::new(0)),
            messages_sent: Arc::new(AtomicU64::new(0)),
            messages_received: Arc::new(AtomicU64::new(0)),
            bytes_sent: Arc::new(AtomicU64::new(0)),
            bytes_received: Arc::new(AtomicU64::new(0)),
            connection_errors: Arc::new(AtomicU64::new(0)),
            protocol_errors: Arc::new(AtomicU64::new(0)),
            application_errors: Arc::new(AtomicU64::new(0)),
            latency_histogram: Arc::new(RwLock::new(LatencyHistogram::new(10000))),
        }
    }
    
    pub fn connection_opened(&self) {
        self.active_connections.fetch_add(1, Ordering::Relaxed);
        self.total_connections.fetch_add(1, Ordering::Relaxed);
        
        info!(
            event = "connection_opened",
            active = self.active_connections.load(Ordering::Relaxed),
            total = self.total_connections.load(Ordering::Relaxed),
            "WebSocket connection established"
        );
    }
    
    pub fn connection_closed(&self, duration: Duration) {
        self.active_connections.fetch_sub(1, Ordering::Relaxed);
        self.total_disconnections.fetch_add(1, Ordering::Relaxed);
        
        info!(
            event = "connection_closed",
            active = self.active_connections.load(Ordering::Relaxed),
            duration_secs = duration.as_secs(),
            "WebSocket connection closed"
        );
    }
    
    pub fn message_sent(&self, bytes: u64) {
        self.messages_sent.fetch_add(1, Ordering::Relaxed);
        self.bytes_sent.fetch_add(bytes, Ordering::Relaxed);
        
        debug!(
            event = "message_sent",
            bytes = bytes,
            total_messages = self.messages_sent.load(Ordering::Relaxed),
            "Sent WebSocket message"
        );
    }
    
    pub fn message_received(&self, bytes: u64) {
        self.messages_received.fetch_add(1, Ordering::Relaxed);
        self.bytes_received.fetch_add(bytes, Ordering::Relaxed);
        
        debug!(
            event = "message_received",
            bytes = bytes,
            total_messages = self.messages_received.load(Ordering::Relaxed),
            "Received WebSocket message"
        );
    }
    
    pub fn record_latency(&self, latency: Duration) {
        let latency_ms = latency.as_secs_f64() * 1000.0;
        
        if let Ok(mut histogram) = self.latency_histogram.write() {
            histogram.record(latency_ms);
        }
        
        if latency_ms > 100.0 {
            warn!(
                event = "high_latency",
                latency_ms = latency_ms,
                "Message latency exceeded threshold"
            );
        }
    }
    
    pub fn record_error(&self, error_type: &str, message: &str) {
        match error_type {
            "connection" => {
                self.connection_errors.fetch_add(1, Ordering::Relaxed);
            }
            "protocol" => {
                self.protocol_errors.fetch_add(1, Ordering::Relaxed);
            }
            "application" => {
                self.application_errors.fetch_add(1, Ordering::Relaxed);
            }
            _ => {}
        }
        
        error!(
            event = "error",
            error_type = error_type,
            message = message,
            "WebSocket error occurred"
        );
    }
    
    pub fn export_prometheus(&self) -> String {
        let mut output = String::new();
        
        output.push_str("# HELP websocket_active_connections Current active connections\n");
        output.push_str("# TYPE websocket_active_connections gauge\n");
        output.push_str(&format!(
            "websocket_active_connections {}\n\n",
            self.active_connections.load(Ordering::Relaxed)
        ));
        
        output.push_str("# HELP websocket_total_connections Total connections opened\n");
        output.push_str("# TYPE websocket_total_connections counter\n");
        output.push_str(&format!(
            "websocket_total_connections {}\n\n",
            self.total_connections.load(Ordering::Relaxed)
        ));
        
        output.push_str("# HELP websocket_messages_sent_total Total messages sent\n");
        output.push_str("# TYPE websocket_messages_sent_total counter\n");
        output.push_str(&format!(
            "websocket_messages_sent_total {}\n\n",
            self.messages_sent.load(Ordering::Relaxed)
        ));
        
        output.push_str("# HELP websocket_messages_received_total Total messages received\n");
        output.push_str("# TYPE websocket_messages_received_total counter\n");
        output.push_str(&format!(
            "websocket_messages_received_total {}\n\n",
            self.messages_received.load(Ordering::Relaxed)
        ));
        
        if let Ok(histogram) = self.latency_histogram.read() {
            if let Some(avg) = histogram.average() {
                output.push_str("# HELP websocket_latency_avg_ms Average latency\n");
                output.push_str("# TYPE websocket_latency_avg_ms gauge\n");
                output.push_str(&format!("websocket_latency_avg_ms {:.2}\n\n", avg));
            }
            
            if let Some(p50) = histogram.percentile(50.0) {
                output.push_str("# HELP websocket_latency_p50_ms 50th percentile latency\n");
                output.push_str("# TYPE websocket_latency_p50_ms gauge\n");
                output.push_str(&format!("websocket_latency_p50_ms {:.2}\n\n", p50));
            }
            
            if let Some(p95) = histogram.percentile(95.0) {
                output.push_str("# HELP websocket_latency_p95_ms 95th percentile latency\n");
                output.push_str("# TYPE websocket_latency_p95_ms gauge\n");
                output.push_str(&format!("websocket_latency_p95_ms {:.2}\n\n", p95));
            }
            
            if let Some(p99) = histogram.percentile(99.0) {
                output.push_str("# HELP websocket_latency_p99_ms 99th percentile latency\n");
                output.push_str("# TYPE websocket_latency_p99_ms gauge\n");
                output.push_str(&format!("websocket_latency_p99_ms {:.2}\n\n", p99));
            }
        }
        
        output.push_str("# HELP websocket_errors_total Total errors by type\n");
        output.push_str("# TYPE websocket_errors_total counter\n");
        output.push_str(&format!(
            "websocket_errors_total{{type=\"connection\"}} {}\n",
            self.connection_errors.load(Ordering::Relaxed)
        ));
        output.push_str(&format!(
            "websocket_errors_total{{type=\"protocol\"}} {}\n",
            self.protocol_errors.load(Ordering::Relaxed)
        ));
        output.push_str(&format!(
            "websocket_errors_total{{type=\"application\"}} {}\n",
            self.application_errors.load(Ordering::Relaxed)
        ));
        
        output
    }
    
    pub fn snapshot(&self) -> MetricsSnapshot {
        let histogram = self.latency_histogram.read().unwrap();
        
        MetricsSnapshot {
            active_connections: self.active_connections.load(Ordering::Relaxed),
            total_connections: self.total_connections.load(Ordering::Relaxed),
            total_disconnections: self.total_disconnections.load(Ordering::Relaxed),
            messages_sent: self.messages_sent.load(Ordering::Relaxed),
            messages_received: self.messages_received.load(Ordering::Relaxed),
            bytes_sent: self.bytes_sent.load(Ordering::Relaxed),
            bytes_received: self.bytes_received.load(Ordering::Relaxed),
            connection_errors: self.connection_errors.load(Ordering::Relaxed),
            protocol_errors: self.protocol_errors.load(Ordering::Relaxed),
            application_errors: self.application_errors.load(Ordering::Relaxed),
            avg_latency_ms: histogram.average(),
            p50_latency_ms: histogram.percentile(50.0),
            p95_latency_ms: histogram.percentile(95.0),
            p99_latency_ms: histogram.percentile(99.0),
        }
    }
}

#[derive(Debug, Serialize)]
pub struct MetricsSnapshot {
    pub active_connections: i64,
    pub total_connections: u64,
    pub total_disconnections: u64,
    pub messages_sent: u64,
    pub messages_received: u64,
    pub bytes_sent: u64,
    pub bytes_received: u64,
    pub connection_errors: u64,
    pub protocol_errors: u64,
    pub application_errors: u64,
    pub avg_latency_ms: Option<f64>,
    pub p50_latency_ms: Option<f64>,
    pub p95_latency_ms: Option<f64>,
    pub p99_latency_ms: Option<f64>,
}

// Initialize structured logging
pub fn init_logging() {
    tracing_subscriber::registry()
        .with(EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info")))
        .with(
            fmt::layer()
                .json()
                .with_current_span(true)
                .with_span_list(false)
        )
        .init();
}

// Example usage
fn main() {
    init_logging();
    
    let metrics = WebSocketMetrics::new();
    
    info!(event = "server_startup", port = 8080, "WebSocket server starting");
    
    // Simulate connection
    let start = Instant::now();
    metrics.connection_opened();
    
    // Simulate messages
    for i in 0..5 {
        metrics.message_received(256);
        metrics.record_latency(Duration::from_millis(15 + i * 2));
        std::thread::sleep(Duration::from_millis(100));
        metrics.message_sent(128);
    }
    
    // Simulate error
    metrics.record_error("protocol", "Invalid frame opcode 0x0F");
    
    // Close connection
    let duration = start.elapsed();
    metrics.connection_closed(duration);
    
    // Export metrics
    println!("\n=== Prometheus Metrics ===\n{}", metrics.export_prometheus());
    
    // JSON snapshot
    let snapshot = metrics.snapshot();
    println!("\n=== Metrics Snapshot ===\n{}", 
             serde_json::to_string_pretty(&snapshot).unwrap());
}
```

## Summary

**Logging and monitoring are essential for production WebSocket systems** because of their long-lived, stateful nature. Key practices include:

**Structured Logging**: Use JSON or similar formats for machine-parsable logs that enable powerful querying and analysis. Include timestamps, log levels, components, events, and contextual fields.

**Comprehensive Metrics**: Track connection lifecycle (active, total, duration), message flow (rate, volume, size), performance (latency percentiles, processing time), and errors (by type and frequency).

**Thread-Safe Collection**: Use atomic operations and locks appropriately to collect metrics from concurrent connections without performance degradation.

**Standard Export Formats**: Support Prometheus or similar formats for integration with monitoring platforms like Grafana, Datadog, or CloudWatch.

**Performance Awareness**: Keep logging overhead minimal - use appropriate log levels, async logging where possible, and avoid excessive detail in hot paths.

**Actionable Alerts**: Define thresholds for key metrics (high latency, error rates, connection failures) to enable proactive incident response.

Both C and Rust implementations demonstrate thread-safe metric collection, structured logging, and standard export formats. Rust's type system and ecosystem (tracing, serde) provide more ergonomic tools, while C requires more manual management but offers maximum control and portability.