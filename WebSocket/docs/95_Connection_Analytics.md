# WebSocket Connection Analytics: Detailed Description

## Overview

Connection Analytics in WebSocket systems involves monitoring, collecting, and analyzing data about client connections, usage patterns, user behavior, and system performance. This enables administrators and developers to understand how their WebSocket infrastructure is being used, identify issues, optimize resource allocation, and make data-driven decisions about scaling and improvements.

## Key Concepts

### What is Connection Analytics?

Connection Analytics encompasses:
- **Connection Metrics**: Tracking connections, disconnections, duration, and frequency
- **User Behavior**: Analyzing message patterns, activity levels, and interaction flows
- **System Usage**: Monitoring resource consumption, bandwidth, and performance
- **Pattern Recognition**: Identifying trends, anomalies, and usage patterns
- **Business Intelligence**: Deriving actionable insights from connection data

### Why It Matters

- **Performance Optimization**: Identify bottlenecks and optimize resource allocation
- **Capacity Planning**: Forecast infrastructure needs based on usage trends
- **User Experience**: Understand and improve client interaction patterns
- **Security**: Detect unusual patterns that may indicate attacks or abuse
- **Cost Management**: Optimize infrastructure spending based on actual usage
- **Product Decisions**: Make informed decisions about features and improvements

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

// Connection event types
typedef enum {
    EVENT_CONNECT,
    EVENT_DISCONNECT,
    EVENT_MESSAGE_SENT,
    EVENT_MESSAGE_RECEIVED,
    EVENT_ERROR,
    EVENT_HEARTBEAT
} EventType;

// Connection statistics
typedef struct {
    char client_id[64];
    time_t connect_time;
    time_t disconnect_time;
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t errors;
    char user_agent[256];
    char ip_address[46]; // IPv6 compatible
    double avg_message_size;
    double connection_duration;
} ConnectionStats;

// Aggregated metrics
typedef struct {
    uint32_t total_connections;
    uint32_t active_connections;
    uint32_t peak_connections;
    uint64_t total_messages;
    uint64_t total_bytes;
    double avg_connection_duration;
    double messages_per_second;
    time_t last_updated;
} AggregateMetrics;

// Time-series data point
typedef struct {
    time_t timestamp;
    uint32_t active_connections;
    uint32_t messages_count;
    uint64_t bytes_transferred;
} TimeSeriesPoint;

// Analytics engine
typedef struct {
    ConnectionStats *connections;
    size_t connection_count;
    size_t connection_capacity;
    AggregateMetrics metrics;
    TimeSeriesPoint *timeseries;
    size_t timeseries_count;
    size_t timeseries_capacity;
    pthread_mutex_t lock;
    FILE *log_file;
} AnalyticsEngine;

// Initialize analytics engine
AnalyticsEngine* analytics_init(const char *log_path) {
    AnalyticsEngine *engine = calloc(1, sizeof(AnalyticsEngine));
    if (!engine) return NULL;
    
    engine->connection_capacity = 1000;
    engine->connections = calloc(engine->connection_capacity, sizeof(ConnectionStats));
    
    engine->timeseries_capacity = 10000;
    engine->timeseries = calloc(engine->timeseries_capacity, sizeof(TimeSeriesPoint));
    
    pthread_mutex_init(&engine->lock, NULL);
    
    if (log_path) {
        engine->log_file = fopen(log_path, "a");
    }
    
    engine->metrics.last_updated = time(NULL);
    
    printf("[Analytics] Engine initialized\n");
    return engine;
}

// Record connection event
void analytics_record_connection(AnalyticsEngine *engine, const char *client_id,
                                 const char *ip, const char *user_agent) {
    pthread_mutex_lock(&engine->lock);
    
    if (engine->connection_count >= engine->connection_capacity) {
        engine->connection_capacity *= 2;
        engine->connections = realloc(engine->connections,
                                     engine->connection_capacity * sizeof(ConnectionStats));
    }
    
    ConnectionStats *stats = &engine->connections[engine->connection_count++];
    strncpy(stats->client_id, client_id, sizeof(stats->client_id) - 1);
    strncpy(stats->ip_address, ip, sizeof(stats->ip_address) - 1);
    strncpy(stats->user_agent, user_agent, sizeof(stats->user_agent) - 1);
    stats->connect_time = time(NULL);
    
    engine->metrics.total_connections++;
    engine->metrics.active_connections++;
    
    if (engine->metrics.active_connections > engine->metrics.peak_connections) {
        engine->metrics.peak_connections = engine->metrics.active_connections;
    }
    
    if (engine->log_file) {
        fprintf(engine->log_file, "%ld,CONNECT,%s,%s\n",
                stats->connect_time, client_id, ip);
        fflush(engine->log_file);
    }
    
    pthread_mutex_unlock(&engine->lock);
    
    printf("[Analytics] Connection: %s from %s\n", client_id, ip);
}

// Record disconnection
void analytics_record_disconnect(AnalyticsEngine *engine, const char *client_id) {
    pthread_mutex_lock(&engine->lock);
    
    // Find connection stats
    for (size_t i = 0; i < engine->connection_count; i++) {
        if (strcmp(engine->connections[i].client_id, client_id) == 0) {
            ConnectionStats *stats = &engine->connections[i];
            stats->disconnect_time = time(NULL);
            stats->connection_duration = difftime(stats->disconnect_time,
                                                  stats->connect_time);
            
            engine->metrics.active_connections--;
            
            // Update average connection duration
            double total_duration = engine->metrics.avg_connection_duration *
                                   (engine->metrics.total_connections - 1);
            total_duration += stats->connection_duration;
            engine->metrics.avg_connection_duration = 
                total_duration / engine->metrics.total_connections;
            
            if (engine->log_file) {
                fprintf(engine->log_file, "%ld,DISCONNECT,%s,%.2f\n",
                        stats->disconnect_time, client_id, stats->connection_duration);
                fflush(engine->log_file);
            }
            
            break;
        }
    }
    
    pthread_mutex_unlock(&engine->lock);
    
    printf("[Analytics] Disconnection: %s\n", client_id);
}

// Record message
void analytics_record_message(AnalyticsEngine *engine, const char *client_id,
                              bool is_sent, size_t size) {
    pthread_mutex_lock(&engine->lock);
    
    for (size_t i = 0; i < engine->connection_count; i++) {
        if (strcmp(engine->connections[i].client_id, client_id) == 0) {
            ConnectionStats *stats = &engine->connections[i];
            
            if (is_sent) {
                stats->messages_sent++;
                stats->bytes_sent += size;
            } else {
                stats->messages_received++;
                stats->bytes_received += size;
            }
            
            uint64_t total_messages = stats->messages_sent + stats->messages_received;
            uint64_t total_bytes = stats->bytes_sent + stats->bytes_received;
            stats->avg_message_size = (double)total_bytes / total_messages;
            
            engine->metrics.total_messages++;
            engine->metrics.total_bytes += size;
            
            break;
        }
    }
    
    pthread_mutex_unlock(&engine->lock);
}

// Add time-series data point
void analytics_add_timeseries_point(AnalyticsEngine *engine) {
    pthread_mutex_lock(&engine->lock);
    
    if (engine->timeseries_count >= engine->timeseries_capacity) {
        // Remove oldest half of data points
        size_t keep = engine->timeseries_capacity / 2;
        memmove(engine->timeseries,
                engine->timeseries + keep,
                keep * sizeof(TimeSeriesPoint));
        engine->timeseries_count = keep;
    }
    
    TimeSeriesPoint *point = &engine->timeseries[engine->timeseries_count++];
    point->timestamp = time(NULL);
    point->active_connections = engine->metrics.active_connections;
    
    // Calculate messages in last interval
    static time_t last_timestamp = 0;
    static uint64_t last_total_messages = 0;
    
    if (last_timestamp > 0) {
        double interval = difftime(point->timestamp, last_timestamp);
        uint64_t messages_delta = engine->metrics.total_messages - last_total_messages;
        engine->metrics.messages_per_second = messages_delta / interval;
    }
    
    last_timestamp = point->timestamp;
    last_total_messages = engine->metrics.total_messages;
    
    pthread_mutex_unlock(&engine->lock);
}

// Generate analytics report
void analytics_generate_report(AnalyticsEngine *engine) {
    pthread_mutex_lock(&engine->lock);
    
    printf("\n========== WebSocket Analytics Report ==========\n");
    printf("Report Time: %s", ctime(&engine->metrics.last_updated));
    printf("\n--- Connection Metrics ---\n");
    printf("Total Connections:   %u\n", engine->metrics.total_connections);
    printf("Active Connections:  %u\n", engine->metrics.active_connections);
    printf("Peak Connections:    %u\n", engine->metrics.peak_connections);
    printf("Avg Duration:        %.2f seconds\n", engine->metrics.avg_connection_duration);
    
    printf("\n--- Traffic Metrics ---\n");
    printf("Total Messages:      %lu\n", engine->metrics.total_messages);
    printf("Total Bytes:         %lu (%.2f MB)\n",
           engine->metrics.total_bytes,
           engine->metrics.total_bytes / (1024.0 * 1024.0));
    printf("Messages/Second:     %.2f\n", engine->metrics.messages_per_second);
    
    // Find top active connections
    printf("\n--- Top Active Connections ---\n");
    ConnectionStats *sorted[10] = {0};
    int top_count = 0;
    
    for (size_t i = 0; i < engine->connection_count && top_count < 10; i++) {
        ConnectionStats *stats = &engine->connections[i];
        if (stats->disconnect_time == 0) { // Active connection
            sorted[top_count++] = stats;
        }
    }
    
    // Simple bubble sort for top connections
    for (int i = 0; i < top_count - 1; i++) {
        for (int j = 0; j < top_count - i - 1; j++) {
            uint64_t total1 = sorted[j]->messages_sent + sorted[j]->messages_received;
            uint64_t total2 = sorted[j+1]->messages_sent + sorted[j+1]->messages_received;
            if (total1 < total2) {
                ConnectionStats *temp = sorted[j];
                sorted[j] = sorted[j+1];
                sorted[j+1] = temp;
            }
        }
    }
    
    for (int i = 0; i < top_count; i++) {
        uint64_t total = sorted[i]->messages_sent + sorted[i]->messages_received;
        printf("%d. %s: %lu messages (%.2f KB)\n",
               i + 1, sorted[i]->client_id, total,
               (sorted[i]->bytes_sent + sorted[i]->bytes_received) / 1024.0);
    }
    
    printf("\n================================================\n\n");
    
    pthread_mutex_unlock(&engine->lock);
}

// Export analytics to JSON
void analytics_export_json(AnalyticsEngine *engine, const char *filename) {
    pthread_mutex_lock(&engine->lock);
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        pthread_mutex_unlock(&engine->lock);
        return;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "  \"timestamp\": %ld,\n", time(NULL));
    fprintf(f, "  \"metrics\": {\n");
    fprintf(f, "    \"total_connections\": %u,\n", engine->metrics.total_connections);
    fprintf(f, "    \"active_connections\": %u,\n", engine->metrics.active_connections);
    fprintf(f, "    \"peak_connections\": %u,\n", engine->metrics.peak_connections);
    fprintf(f, "    \"total_messages\": %lu,\n", engine->metrics.total_messages);
    fprintf(f, "    \"total_bytes\": %lu,\n", engine->metrics.total_bytes);
    fprintf(f, "    \"avg_connection_duration\": %.2f,\n", engine->metrics.avg_connection_duration);
    fprintf(f, "    \"messages_per_second\": %.2f\n", engine->metrics.messages_per_second);
    fprintf(f, "  },\n");
    
    fprintf(f, "  \"connections\": [\n");
    for (size_t i = 0; i < engine->connection_count; i++) {
        ConnectionStats *s = &engine->connections[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"client_id\": \"%s\",\n", s->client_id);
        fprintf(f, "      \"ip\": \"%s\",\n", s->ip_address);
        fprintf(f, "      \"messages_sent\": %lu,\n", s->messages_sent);
        fprintf(f, "      \"messages_received\": %lu,\n", s->messages_received);
        fprintf(f, "      \"bytes_sent\": %lu,\n", s->bytes_sent);
        fprintf(f, "      \"bytes_received\": %lu\n", s->bytes_received);
        fprintf(f, "    }%s\n", (i < engine->connection_count - 1) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    
    fclose(f);
    pthread_mutex_unlock(&engine->lock);
    
    printf("[Analytics] Exported to %s\n", filename);
}

// Cleanup
void analytics_destroy(AnalyticsEngine *engine) {
    if (!engine) return;
    
    if (engine->log_file) {
        fclose(engine->log_file);
    }
    
    free(engine->connections);
    free(engine->timeseries);
    pthread_mutex_destroy(&engine->lock);
    free(engine);
}

// Example usage
int main() {
    AnalyticsEngine *analytics = analytics_init("websocket_analytics.log");
    
    // Simulate connections
    analytics_record_connection(analytics, "client_001", "192.168.1.100", "Mozilla/5.0");
    analytics_record_connection(analytics, "client_002", "192.168.1.101", "Chrome/90.0");
    analytics_record_connection(analytics, "client_003", "192.168.1.102", "Safari/14.0");
    
    // Simulate messages
    for (int i = 0; i < 50; i++) {
        analytics_record_message(analytics, "client_001", true, 256);
        analytics_record_message(analytics, "client_001", false, 128);
        analytics_record_message(analytics, "client_002", true, 512);
        analytics_record_message(analytics, "client_003", false, 1024);
    }
    
    // Add time-series point
    analytics_add_timeseries_point(analytics);
    
    // Disconnect one client
    analytics_record_disconnect(analytics, "client_002");
    
    // Generate report
    analytics_generate_report(analytics);
    
    // Export to JSON
    analytics_export_json(analytics, "analytics_report.json");
    
    analytics_destroy(analytics);
    
    return 0;
}
```

---

## Rust Implementation

```rust
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs::File;
use std::io::Write;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub enum EventType {
    Connect,
    Disconnect,
    MessageSent,
    MessageReceived,
    Error,
    Heartbeat,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConnectionStats {
    pub client_id: String,
    pub ip_address: String,
    pub user_agent: String,
    pub connect_time: u64,
    pub disconnect_time: Option<u64>,
    pub messages_sent: u64,
    pub messages_received: u64,
    pub bytes_sent: u64,
    pub bytes_received: u64,
    pub errors: u32,
    pub avg_message_size: f64,
    pub connection_duration: Option<f64>,
}

impl ConnectionStats {
    fn new(client_id: String, ip: String, user_agent: String) -> Self {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();

        Self {
            client_id,
            ip_address: ip,
            user_agent,
            connect_time: now,
            disconnect_time: None,
            messages_sent: 0,
            messages_received: 0,
            bytes_sent: 0,
            bytes_received: 0,
            errors: 0,
            avg_message_size: 0.0,
            connection_duration: None,
        }
    }

    fn record_message(&mut self, is_sent: bool, size: u64) {
        if is_sent {
            self.messages_sent += 1;
            self.bytes_sent += size;
        } else {
            self.messages_received += 1;
            self.bytes_received += size;
        }

        let total_messages = self.messages_sent + self.messages_received;
        let total_bytes = self.bytes_sent + self.bytes_received;
        self.avg_message_size = total_bytes as f64 / total_messages as f64;
    }

    fn disconnect(&mut self) {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();

        self.disconnect_time = Some(now);
        self.connection_duration = Some((now - self.connect_time) as f64);
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AggregateMetrics {
    pub total_connections: u32,
    pub active_connections: u32,
    pub peak_connections: u32,
    pub total_messages: u64,
    pub total_bytes: u64,
    pub avg_connection_duration: f64,
    pub messages_per_second: f64,
    pub last_updated: u64,
}

impl Default for AggregateMetrics {
    fn default() -> Self {
        Self {
            total_connections: 0,
            active_connections: 0,
            peak_connections: 0,
            total_messages: 0,
            total_bytes: 0,
            avg_connection_duration: 0.0,
            messages_per_second: 0.0,
            last_updated: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TimeSeriesPoint {
    pub timestamp: u64,
    pub active_connections: u32,
    pub messages_count: u64,
    pub bytes_transferred: u64,
}

#[derive(Debug, Clone)]
pub struct AnalyticsEngine {
    connections: Arc<Mutex<HashMap<String, ConnectionStats>>>,
    metrics: Arc<Mutex<AggregateMetrics>>,
    timeseries: Arc<Mutex<Vec<TimeSeriesPoint>>>,
    log_file: Arc<Mutex<Option<File>>>,
    last_message_count: Arc<Mutex<u64>>,
    last_timestamp: Arc<Mutex<u64>>,
}

impl AnalyticsEngine {
    pub fn new(log_path: Option<&str>) -> Self {
        let log_file = log_path.and_then(|path| {
            File::options()
                .create(true)
                .append(true)
                .open(path)
                .ok()
        });

        Self {
            connections: Arc::new(Mutex::new(HashMap::new())),
            metrics: Arc::new(Mutex::new(AggregateMetrics::default())),
            timeseries: Arc::new(Mutex::new(Vec::new())),
            log_file: Arc::new(Mutex::new(log_file)),
            last_message_count: Arc::new(Mutex::new(0)),
            last_timestamp: Arc::new(Mutex::new(0)),
        }
    }

    pub fn record_connection(&self, client_id: &str, ip: &str, user_agent: &str) {
        let mut connections = self.connections.lock().unwrap();
        let mut metrics = self.metrics.lock().unwrap();

        let stats = ConnectionStats::new(
            client_id.to_string(),
            ip.to_string(),
            user_agent.to_string(),
        );

        connections.insert(client_id.to_string(), stats.clone());

        metrics.total_connections += 1;
        metrics.active_connections += 1;

        if metrics.active_connections > metrics.peak_connections {
            metrics.peak_connections = metrics.active_connections;
        }

        if let Some(ref mut file) = *self.log_file.lock().unwrap() {
            let _ = writeln!(
                file,
                "{},CONNECT,{},{}",
                stats.connect_time, client_id, ip
            );
        }

        println!("[Analytics] Connection: {} from {}", client_id, ip);
    }

    pub fn record_disconnect(&self, client_id: &str) {
        let mut connections = self.connections.lock().unwrap();
        let mut metrics = self.metrics.lock().unwrap();

        if let Some(stats) = connections.get_mut(client_id) {
            stats.disconnect();
            metrics.active_connections -= 1;

            if let Some(duration) = stats.connection_duration {
                let total_duration = metrics.avg_connection_duration
                    * (metrics.total_connections - 1) as f64;
                metrics.avg_connection_duration =
                    (total_duration + duration) / metrics.total_connections as f64;
            }

            if let Some(ref mut file) = *self.log_file.lock().unwrap() {
                let _ = writeln!(
                    file,
                    "{},DISCONNECT,{},{:.2}",
                    stats.disconnect_time.unwrap(),
                    client_id,
                    stats.connection_duration.unwrap()
                );
            }

            println!("[Analytics] Disconnection: {}", client_id);
        }
    }

    pub fn record_message(&self, client_id: &str, is_sent: bool, size: u64) {
        let mut connections = self.connections.lock().unwrap();
        let mut metrics = self.metrics.lock().unwrap();

        if let Some(stats) = connections.get_mut(client_id) {
            stats.record_message(is_sent, size);
            metrics.total_messages += 1;
            metrics.total_bytes += size;
        }
    }

    pub fn add_timeseries_point(&self) {
        let metrics = self.metrics.lock().unwrap();
        let mut timeseries = self.timeseries.lock().unwrap();

        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();

        let point = TimeSeriesPoint {
            timestamp: now,
            active_connections: metrics.active_connections,
            messages_count: metrics.total_messages,
            bytes_transferred: metrics.total_bytes,
        };

        timeseries.push(point);

        // Keep only last 10000 points
        if timeseries.len() > 10000 {
            timeseries.drain(0..5000);
        }

        // Calculate messages per second
        let mut last_count = self.last_message_count.lock().unwrap();
        let mut last_ts = self.last_timestamp.lock().unwrap();

        if *last_ts > 0 {
            let interval = (now - *last_ts) as f64;
            let messages_delta = metrics.total_messages - *last_count;
            drop(metrics); // Release lock before acquiring mutable reference
            let mut metrics = self.metrics.lock().unwrap();
            metrics.messages_per_second = messages_delta as f64 / interval;
        }

        *last_ts = now;
        *last_count = self.metrics.lock().unwrap().total_messages;
    }

    pub fn generate_report(&self) {
        let connections = self.connections.lock().unwrap();
        let metrics = self.metrics.lock().unwrap();

        println!("\n========== WebSocket Analytics Report ==========");
        println!(
            "Report Time: {}",
            chrono::DateTime::from_timestamp(metrics.last_updated as i64, 0)
                .unwrap()
                .format("%Y-%m-%d %H:%M:%S")
        );

        println!("\n--- Connection Metrics ---");
        println!("Total Connections:   {}", metrics.total_connections);
        println!("Active Connections:  {}", metrics.active_connections);
        println!("Peak Connections:    {}", metrics.peak_connections);
        println!(
            "Avg Duration:        {:.2} seconds",
            metrics.avg_connection_duration
        );

        println!("\n--- Traffic Metrics ---");
        println!("Total Messages:      {}", metrics.total_messages);
        println!(
            "Total Bytes:         {} ({:.2} MB)",
            metrics.total_bytes,
            metrics.total_bytes as f64 / (1024.0 * 1024.0)
        );
        println!("Messages/Second:     {:.2}", metrics.messages_per_second);

        // Top active connections
        println!("\n--- Top Active Connections ---");
        let mut active: Vec<_> = connections
            .values()
            .filter(|s| s.disconnect_time.is_none())
            .collect();

        active.sort_by(|a, b| {
            let total_a = a.messages_sent + a.messages_received;
            let total_b = b.messages_sent + b.messages_received;
            total_b.cmp(&total_a)
        });

        for (i, stats) in active.iter().take(10).enumerate() {
            let total = stats.messages_sent + stats.messages_received;
            let total_kb = (stats.bytes_sent + stats.bytes_received) as f64 / 1024.0;
            println!(
                "{}. {}: {} messages ({:.2} KB)",
                i + 1,
                stats.client_id,
                total,
                total_kb
            );
        }

        println!("\n================================================\n");
    }

    pub fn export_json(&self, filename: &str) -> std::io::Result<()> {
        let connections = self.connections.lock().unwrap();
        let metrics = self.metrics.lock().unwrap();

        #[derive(Serialize)]
        struct Report {
            timestamp: u64,
            metrics: AggregateMetrics,
            connections: Vec<ConnectionStats>,
        }

        let report = Report {
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs(),
            metrics: metrics.clone(),
            connections: connections.values().cloned().collect(),
        };

        let json = serde_json::to_string_pretty(&report)?;
        std::fs::write(filename, json)?;

        println!("[Analytics] Exported to {}", filename);
        Ok(())
    }

    pub fn get_metrics(&self) -> AggregateMetrics {
        self.metrics.lock().unwrap().clone()
    }

    pub fn get_connection_stats(&self, client_id: &str) -> Option<ConnectionStats> {
        self.connections
            .lock()
            .unwrap()
            .get(client_id)
            .cloned()
    }
}

fn main() {
    let analytics = AnalyticsEngine::new(Some("websocket_analytics.log"));

    // Simulate connections
    analytics.record_connection("client_001", "192.168.1.100", "Mozilla/5.0");
    analytics.record_connection("client_002", "192.168.1.101", "Chrome/90.0");
    analytics.record_connection("client_003", "192.168.1.102", "Safari/14.0");

    // Simulate messages
    for _ in 0..50 {
        analytics.record_message("client_001", true, 256);
        analytics.record_message("client_001", false, 128);
        analytics.record_message("client_002", true, 512);
        analytics.record_message("client_003", false, 1024);
    }

    // Add time-series point
    analytics.add_timeseries_point();

    // Disconnect one client
    analytics.record_disconnect("client_002");

    // Generate report
    analytics.generate_report();

    // Export to JSON
    analytics.export_json("analytics_report.json").unwrap();
}
```

---

## Summary

**Connection Analytics for WebSocket systems** provides crucial insights into how real-time applications are being used. Key takeaways:

### Core Components
1. **Connection Tracking** - Monitor connect/disconnect events, durations, and patterns
2. **Message Analytics** - Track message frequency, size, and throughput
3. **User Behavior** - Analyze interaction patterns and engagement levels
4. **System Metrics** - Monitor resource usage, performance, and capacity

### Implementation Features
- **Real-time Monitoring** - Track active connections and current metrics
- **Time-Series Data** - Historical data for trend analysis
- **Aggregate Metrics** - Summary statistics for quick insights
- **Export Capabilities** - JSON export for integration with BI tools
- **Top N Analysis** - Identify most active connections and patterns

### Use Cases
- **Performance Optimization** - Identify bottlenecks and optimize resource allocation
- **Capacity Planning** - Forecast infrastructure needs based on trends
- **Security Monitoring** - Detect anomalies and potential attacks
- **User Experience** - Understand and improve client interactions
- **Business Intelligence** - Make data-driven product decisions

### Best Practices
- Use thread-safe data structures for concurrent access
- Implement efficient storage with circular buffers for time-series data
- Log critical events for audit trails and debugging
- Export data in standard formats (JSON, CSV) for analysis
- Calculate rolling averages and percentiles for better insights
- Implement data retention policies to manage storage

Connection analytics transforms raw WebSocket data into actionable intelligence, enabling better decision-making and system optimization.