# MQTT Monitoring and Alerting: A Comprehensive Guide

## Overview

Monitoring and alerting are critical components of production MQTT deployments. They enable you to track system health, detect anomalies, identify performance bottlenecks, and respond to issues before they impact users. This involves collecting metrics from MQTT brokers and clients, visualizing data, and configuring alerts for critical thresholds.

## Key Concepts

### 1. **Metrics Categories**

- **Broker Metrics**: Connection count, message throughput, queue sizes, memory usage
- **Client Metrics**: Connection status, message delivery success/failure, latency
- **System Metrics**: CPU, memory, disk I/O, network bandwidth
- **Application Metrics**: Custom business logic metrics, processing times

### 2. **Monitoring Approaches**

- **Push-based**: Clients/brokers push metrics to monitoring systems
- **Pull-based**: Monitoring systems scrape metrics endpoints (Prometheus-style)
- **Log-based**: Parse logs to extract metrics and events
- **MQTT-based**: Publish metrics to special topics ($SYS for brokers)

### 3. **Alert Strategies**

- **Threshold-based**: Trigger when metrics exceed defined limits
- **Rate-of-change**: Alert on sudden metric changes
- **Anomaly detection**: Machine learning-based pattern recognition
- **Composite conditions**: Multiple metrics combined with logic

## C/C++ Implementation

### Basic MQTT Client with Metrics Collection

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mosquitto.h>
#include <pthread.h>
#include <stdatomic.h>

// Metrics structure
typedef struct {
    atomic_uint_fast64_t messages_sent;
    atomic_uint_fast64_t messages_received;
    atomic_uint_fast64_t messages_failed;
    atomic_uint_fast64_t connection_count;
    atomic_uint_fast64_t disconnection_count;
    time_t last_message_time;
    pthread_mutex_t time_mutex;
} mqtt_metrics_t;

// Global metrics instance
mqtt_metrics_t g_metrics = {0};

// Initialize metrics
void metrics_init(mqtt_metrics_t *metrics) {
    atomic_init(&metrics->messages_sent, 0);
    atomic_init(&metrics->messages_received, 0);
    atomic_init(&metrics->messages_failed, 0);
    atomic_init(&metrics->connection_count, 0);
    atomic_init(&metrics->disconnection_count, 0);
    metrics->last_message_time = time(NULL);
    pthread_mutex_init(&metrics->time_mutex, NULL);
}

// Callback: Connection established
void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    mqtt_metrics_t *metrics = (mqtt_metrics_t *)obj;
    
    if (rc == 0) {
        printf("Connected successfully\n");
        atomic_fetch_add(&metrics->connection_count, 1);
        
        // Subscribe to monitoring topics
        mosquitto_subscribe(mosq, NULL, "sensor/#", 0);
        mosquitto_subscribe(mosq, NULL, "$SYS/broker/load/#", 0);
    } else {
        fprintf(stderr, "Connection failed: %s\n", mosquitto_connack_string(rc));
        atomic_fetch_add(&metrics->messages_failed, 1);
    }
}

// Callback: Message received
void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    mqtt_metrics_t *metrics = (mqtt_metrics_t *)obj;
    
    atomic_fetch_add(&metrics->messages_received, 1);
    
    pthread_mutex_lock(&metrics->time_mutex);
    metrics->last_message_time = time(NULL);
    pthread_mutex_unlock(&metrics->time_mutex);
    
    printf("Received on %s: %.*s\n", msg->topic, 
           msg->payloadlen, (char *)msg->payload);
}

// Callback: Message published
void on_publish(struct mosquitto *mosq, void *obj, int mid) {
    mqtt_metrics_t *metrics = (mqtt_metrics_t *)obj;
    atomic_fetch_add(&metrics->messages_sent, 1);
}

// Callback: Disconnected
void on_disconnect(struct mosquitto *mosq, void *obj, int rc) {
    mqtt_metrics_t *metrics = (mqtt_metrics_t *)obj;
    atomic_fetch_add(&metrics->disconnection_count, 1);
    
    if (rc != 0) {
        fprintf(stderr, "Unexpected disconnection\n");
    }
}

// Publish metrics to MQTT
void publish_metrics(struct mosquitto *mosq, mqtt_metrics_t *metrics) {
    char payload[256];
    time_t now = time(NULL);
    
    pthread_mutex_lock(&metrics->time_mutex);
    double time_since_last = difftime(now, metrics->last_message_time);
    pthread_mutex_unlock(&metrics->time_mutex);
    
    // Publish individual metrics
    snprintf(payload, sizeof(payload), "%lu", 
             atomic_load(&metrics->messages_sent));
    mosquitto_publish(mosq, NULL, "metrics/messages/sent", 
                     strlen(payload), payload, 1, false);
    
    snprintf(payload, sizeof(payload), "%lu", 
             atomic_load(&metrics->messages_received));
    mosquitto_publish(mosq, NULL, "metrics/messages/received", 
                     strlen(payload), payload, 1, false);
    
    snprintf(payload, sizeof(payload), "%lu", 
             atomic_load(&metrics->messages_failed));
    mosquitto_publish(mosq, NULL, "metrics/messages/failed", 
                     strlen(payload), payload, 1, false);
    
    snprintf(payload, sizeof(payload), "%.0f", time_since_last);
    mosquitto_publish(mosq, NULL, "metrics/time_since_last_message", 
                     strlen(payload), payload, 1, false);
}

// Check thresholds and generate alerts
void check_alerts(struct mosquitto *mosq, mqtt_metrics_t *metrics) {
    time_t now = time(NULL);
    
    pthread_mutex_lock(&metrics->time_mutex);
    double time_since_last = difftime(now, metrics->last_message_time);
    pthread_mutex_unlock(&metrics->time_mutex);
    
    // Alert if no messages received in 60 seconds
    if (time_since_last > 60.0) {
        char alert[256];
        snprintf(alert, sizeof(alert), 
                 "{\"severity\":\"warning\",\"message\":\"No messages for %.0f seconds\",\"timestamp\":%ld}",
                 time_since_last, now);
        mosquitto_publish(mosq, NULL, "alerts/no_messages", 
                         strlen(alert), alert, 1, false);
    }
    
    // Alert if failure rate is high
    uint64_t failed = atomic_load(&metrics->messages_failed);
    uint64_t sent = atomic_load(&metrics->messages_sent);
    if (sent > 0 && (failed * 100 / sent) > 5) {
        char alert[256];
        snprintf(alert, sizeof(alert), 
                 "{\"severity\":\"critical\",\"message\":\"High failure rate: %lu%%\",\"timestamp\":%ld}",
                 (failed * 100 / sent), now);
        mosquitto_publish(mosq, NULL, "alerts/high_failure_rate", 
                         strlen(alert), alert, 1, false);
    }
}

// Monitoring thread
void *monitoring_thread(void *arg) {
    struct mosquitto *mosq = (struct mosquitto *)arg;
    
    while (1) {
        sleep(10); // Check every 10 seconds
        
        publish_metrics(mosq, &g_metrics);
        check_alerts(mosq, &g_metrics);
        
        printf("\n=== Metrics ===\n");
        printf("Sent: %lu, Received: %lu, Failed: %lu\n",
               atomic_load(&g_metrics.messages_sent),
               atomic_load(&g_metrics.messages_received),
               atomic_load(&g_metrics.messages_failed));
        printf("Connections: %lu, Disconnections: %lu\n",
               atomic_load(&g_metrics.connection_count),
               atomic_load(&g_metrics.disconnection_count));
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    struct mosquitto *mosq;
    pthread_t monitor_thread;
    
    mosquitto_lib_init();
    metrics_init(&g_metrics);
    
    mosq = mosquitto_new("monitoring_client", true, &g_metrics);
    if (!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_publish_callback_set(mosq, on_publish);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    
    // Connect to broker
    if (mosquitto_connect(mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to connect\n");
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }
    
    // Start monitoring thread
    pthread_create(&monitor_thread, NULL, monitoring_thread, mosq);
    
    // Main loop
    mosquitto_loop_forever(mosq, -1, 1);
    
    // Cleanup
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    pthread_mutex_destroy(&g_metrics.time_mutex);
    
    return 0;
}
```

### Advanced Monitoring with Prometheus Metrics Format

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <mosquitto.h>
#include <stdatomic.h>

// Metrics in Prometheus format
typedef struct {
    atomic_uint_fast64_t mqtt_messages_sent_total;
    atomic_uint_fast64_t mqtt_messages_received_total;
    atomic_uint_fast64_t mqtt_connection_errors_total;
    atomic_int mqtt_connection_status;
    atomic_uint_fast64_t mqtt_message_latency_ms;
} prometheus_metrics_t;

prometheus_metrics_t prom_metrics = {0};

// HTTP handler for /metrics endpoint
static int metrics_handler(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version, const char *upload_data,
                          size_t *upload_data_size, void **con_cls) {
    char response[4096];
    struct MHD_Response *mhd_response;
    int ret;
    
    // Generate Prometheus-format metrics
    snprintf(response, sizeof(response),
        "# HELP mqtt_messages_sent_total Total messages sent\n"
        "# TYPE mqtt_messages_sent_total counter\n"
        "mqtt_messages_sent_total %lu\n"
        "\n"
        "# HELP mqtt_messages_received_total Total messages received\n"
        "# TYPE mqtt_messages_received_total counter\n"
        "mqtt_messages_received_total %lu\n"
        "\n"
        "# HELP mqtt_connection_errors_total Total connection errors\n"
        "# TYPE mqtt_connection_errors_total counter\n"
        "mqtt_connection_errors_total %lu\n"
        "\n"
        "# HELP mqtt_connection_status Connection status (1=connected, 0=disconnected)\n"
        "# TYPE mqtt_connection_status gauge\n"
        "mqtt_connection_status %d\n"
        "\n"
        "# HELP mqtt_message_latency_ms Message latency in milliseconds\n"
        "# TYPE mqtt_message_latency_ms gauge\n"
        "mqtt_message_latency_ms %lu\n",
        atomic_load(&prom_metrics.mqtt_messages_sent_total),
        atomic_load(&prom_metrics.mqtt_messages_received_total),
        atomic_load(&prom_metrics.mqtt_connection_errors_total),
        atomic_load(&prom_metrics.mqtt_connection_status),
        atomic_load(&prom_metrics.mqtt_message_latency_ms)
    );
    
    mhd_response = MHD_create_response_from_buffer(strlen(response),
                                                   (void *)response,
                                                   MHD_RESPMEM_MUST_COPY);
    ret = MHD_queue_response(connection, MHD_HTTP_OK, mhd_response);
    MHD_destroy_response(mhd_response);
    
    return ret;
}

// Start Prometheus metrics HTTP server
struct MHD_Daemon *start_metrics_server(int port) {
    return MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
                           &metrics_handler, NULL, MHD_OPTION_END);
}
```

## Rust Implementation

### Comprehensive Monitoring System with Tokio

```rust
use rumqttc::{AsyncClient, Event, Incoming, MqttOptions, QoS};
use tokio::time::{interval, Duration};
use std::sync::Arc;
use tokio::sync::RwLock;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct Metrics {
    messages_sent: u64,
    messages_received: u64,
    messages_failed: u64,
    connection_count: u64,
    disconnection_count: u64,
    last_message_timestamp: i64,
    message_latencies: Vec<u64>,
    topic_counts: HashMap<String, u64>,
}

impl Metrics {
    fn new() -> Self {
        Self {
            messages_sent: 0,
            messages_received: 0,
            messages_failed: 0,
            connection_count: 0,
            disconnection_count: 0,
            last_message_timestamp: chrono::Utc::now().timestamp(),
            message_latencies: Vec::new(),
            topic_counts: HashMap::new(),
        }
    }

    fn average_latency(&self) -> f64 {
        if self.message_latencies.is_empty() {
            0.0
        } else {
            self.message_latencies.iter().sum::<u64>() as f64 
                / self.message_latencies.len() as f64
        }
    }

    fn failure_rate(&self) -> f64 {
        if self.messages_sent == 0 {
            0.0
        } else {
            (self.messages_failed as f64 / self.messages_sent as f64) * 100.0
        }
    }
}

#[derive(Debug, Clone, Serialize)]
struct Alert {
    severity: AlertSeverity,
    message: String,
    timestamp: i64,
    metric_value: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
enum AlertSeverity {
    Info,
    Warning,
    Critical,
}

struct MonitoringSystem {
    metrics: Arc<RwLock<Metrics>>,
    alert_thresholds: AlertThresholds,
}

struct AlertThresholds {
    max_failure_rate: f64,
    max_latency_ms: u64,
    max_time_without_message: i64,
    min_messages_per_minute: u64,
}

impl Default for AlertThresholds {
    fn default() -> Self {
        Self {
            max_failure_rate: 5.0,
            max_latency_ms: 1000,
            max_time_without_message: 60,
            min_messages_per_minute: 10,
        }
    }
}

impl MonitoringSystem {
    fn new() -> Self {
        Self {
            metrics: Arc::new(RwLock::new(Metrics::new())),
            alert_thresholds: AlertThresholds::default(),
        }
    }

    async fn record_message_sent(&self) {
        let mut metrics = self.metrics.write().await;
        metrics.messages_sent += 1;
    }

    async fn record_message_received(&self, topic: &str, latency_ms: u64) {
        let mut metrics = self.metrics.write().await;
        metrics.messages_received += 1;
        metrics.last_message_timestamp = chrono::Utc::now().timestamp();
        metrics.message_latencies.push(latency_ms);
        
        // Keep only last 1000 latency measurements
        if metrics.message_latencies.len() > 1000 {
            metrics.message_latencies.remove(0);
        }

        *metrics.topic_counts.entry(topic.to_string()).or_insert(0) += 1;
    }

    async fn record_connection(&self) {
        let mut metrics = self.metrics.write().await;
        metrics.connection_count += 1;
    }

    async fn record_disconnection(&self) {
        let mut metrics = self.metrics.write().await;
        metrics.disconnection_count += 1;
    }

    async fn record_failure(&self) {
        let mut metrics = self.metrics.write().await;
        metrics.messages_failed += 1;
    }

    async fn check_alerts(&self) -> Vec<Alert> {
        let metrics = self.metrics.read().await;
        let mut alerts = Vec::new();
        let now = chrono::Utc::now().timestamp();

        // Check failure rate
        let failure_rate = metrics.failure_rate();
        if failure_rate > self.alert_thresholds.max_failure_rate {
            alerts.push(Alert {
                severity: AlertSeverity::Critical,
                message: format!("High failure rate: {:.2}%", failure_rate),
                timestamp: now,
                metric_value: Some(failure_rate),
            });
        }

        // Check message latency
        let avg_latency = metrics.average_latency();
        if avg_latency > self.alert_thresholds.max_latency_ms as f64 {
            alerts.push(Alert {
                severity: AlertSeverity::Warning,
                message: format!("High average latency: {:.2}ms", avg_latency),
                timestamp: now,
                metric_value: Some(avg_latency),
            });
        }

        // Check time since last message
        let time_since_last = now - metrics.last_message_timestamp;
        if time_since_last > self.alert_thresholds.max_time_without_message {
            alerts.push(Alert {
                severity: AlertSeverity::Warning,
                message: format!("No messages for {} seconds", time_since_last),
                timestamp: now,
                metric_value: Some(time_since_last as f64),
            });
        }

        alerts
    }

    async fn get_prometheus_metrics(&self) -> String {
        let metrics = self.metrics.read().await;
        format!(
            "# HELP mqtt_messages_sent_total Total messages sent\n\
             # TYPE mqtt_messages_sent_total counter\n\
             mqtt_messages_sent_total {}\n\
             \n\
             # HELP mqtt_messages_received_total Total messages received\n\
             # TYPE mqtt_messages_received_total counter\n\
             mqtt_messages_received_total {}\n\
             \n\
             # HELP mqtt_messages_failed_total Total failed messages\n\
             # TYPE mqtt_messages_failed_total counter\n\
             mqtt_messages_failed_total {}\n\
             \n\
             # HELP mqtt_connection_count_total Total connections\n\
             # TYPE mqtt_connection_count_total counter\n\
             mqtt_connection_count_total {}\n\
             \n\
             # HELP mqtt_average_latency_ms Average message latency\n\
             # TYPE mqtt_average_latency_ms gauge\n\
             mqtt_average_latency_ms {:.2}\n\
             \n\
             # HELP mqtt_failure_rate_percent Message failure rate\n\
             # TYPE mqtt_failure_rate_percent gauge\n\
             mqtt_failure_rate_percent {:.2}\n",
            metrics.messages_sent,
            metrics.messages_received,
            metrics.messages_failed,
            metrics.connection_count,
            metrics.average_latency(),
            metrics.failure_rate()
        )
    }

    async fn publish_metrics(&self, client: &AsyncClient) -> Result<(), Box<dyn std::error::Error>> {
        let metrics = self.metrics.read().await;
        
        // Publish as JSON
        let json_metrics = serde_json::to_string(&*metrics)?;
        client.publish("metrics/mqtt/all", QoS::AtLeastOnce, false, json_metrics).await?;

        // Publish individual metrics
        client.publish(
            "metrics/mqtt/sent",
            QoS::AtLeastOnce,
            false,
            metrics.messages_sent.to_string(),
        ).await?;

        client.publish(
            "metrics/mqtt/received",
            QoS::AtLeastOnce,
            false,
            metrics.messages_received.to_string(),
        ).await?;

        client.publish(
            "metrics/mqtt/avg_latency",
            QoS::AtLeastOnce,
            false,
            metrics.average_latency().to_string(),
        ).await?;

        Ok(())
    }

    async fn publish_alerts(&self, client: &AsyncClient, alerts: Vec<Alert>) -> Result<(), Box<dyn std::error::Error>> {
        for alert in alerts {
            let json_alert = serde_json::to_string(&alert)?;
            let topic = match alert.severity {
                AlertSeverity::Critical => "alerts/critical",
                AlertSeverity::Warning => "alerts/warning",
                AlertSeverity::Info => "alerts/info",
            };
            client.publish(topic, QoS::AtLeastOnce, false, json_alert).await?;
        }
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("monitoring_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(30));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    let monitoring = Arc::new(MonitoringSystem::new());

    // Subscribe to topics
    client.subscribe("sensor/#", QoS::AtLeastOnce).await?;
    client.subscribe("$SYS/#", QoS::AtLeastOnce).await?;

    // Spawn monitoring task
    let monitoring_clone = monitoring.clone();
    let client_clone = client.clone();
    tokio::spawn(async move {
        let mut ticker = interval(Duration::from_secs(10));
        loop {
            ticker.tick().await;
            
            // Publish metrics
            if let Err(e) = monitoring_clone.publish_metrics(&client_clone).await {
                eprintln!("Failed to publish metrics: {}", e);
            }

            // Check and publish alerts
            let alerts = monitoring_clone.check_alerts().await;
            if !alerts.is_empty() {
                println!("Generated {} alerts", alerts.len());
                if let Err(e) = monitoring_clone.publish_alerts(&client_clone, alerts).await {
                    eprintln!("Failed to publish alerts: {}", e);
                }
            }

            // Print current metrics
            let metrics = monitoring_clone.metrics.read().await;
            println!("\n=== Metrics ===");
            println!("Sent: {}, Received: {}, Failed: {}", 
                     metrics.messages_sent, metrics.messages_received, metrics.messages_failed);
            println!("Avg Latency: {:.2}ms, Failure Rate: {:.2}%", 
                     metrics.average_latency(), metrics.failure_rate());
        }
    });

    // Start Prometheus metrics server
    let monitoring_clone = monitoring.clone();
    tokio::spawn(async move {
        use warp::Filter;
        
        let metrics_route = warp::path("metrics")
            .and(warp::get())
            .and_then(move || {
                let mon = monitoring_clone.clone();
                async move {
                    let metrics = mon.get_prometheus_metrics().await;
                    Ok::<_, warp::Rejection>(metrics)
                }
            });

        warp::serve(metrics_route).run(([0, 0, 0, 0], 9090)).await;
    });

    // Main event loop
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Incoming::ConnAck(_))) => {
                println!("Connected to broker");
                monitoring.record_connection().await;
            }
            Ok(Event::Incoming(Incoming::Publish(p))) => {
                let latency = 10; // In real scenario, calculate from timestamp
                monitoring.record_message_received(&p.topic, latency).await;
            }
            Ok(Event::Incoming(Incoming::Disconnect)) => {
                println!("Disconnected from broker");
                monitoring.record_disconnection().await;
            }
            Ok(Event::Outgoing(rumqttc::Outgoing::Publish(_))) => {
                monitoring.record_message_sent().await;
            }
            Err(e) => {
                eprintln!("Connection error: {}", e);
                monitoring.record_failure().await;
            }
            _ => {}
        }
    }
}
```

### Advanced Rust: Health Check Service

```rust
use axum::{
    extract::State,
    http::StatusCode,
    response::Json,
    routing::get,
    Router,
};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct HealthStatus {
    status: String,
    mqtt_connected: bool,
    last_message_age_seconds: i64,
    total_messages: u64,
    failure_rate: f64,
    checks: Vec<HealthCheck>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct HealthCheck {
    name: String,
    status: String,
    message: Option<String>,
}

struct AppState {
    monitoring: Arc<MonitoringSystem>,
}

async fn health_handler(
    State(state): State<Arc<AppState>>,
) -> (StatusCode, Json<HealthStatus>) {
    let metrics = state.monitoring.metrics.read().await;
    let now = chrono::Utc::now().timestamp();
    let last_message_age = now - metrics.last_message_timestamp;
    
    let mut checks = Vec::new();
    let mut overall_healthy = true;

    // Check message flow
    if last_message_age > 60 {
        checks.push(HealthCheck {
            name: "message_flow".to_string(),
            status: "unhealthy".to_string(),
            message: Some(format!("No messages for {} seconds", last_message_age)),
        });
        overall_healthy = false;
    } else {
        checks.push(HealthCheck {
            name: "message_flow".to_string(),
            status: "healthy".to_string(),
            message: None,
        });
    }

    // Check failure rate
    let failure_rate = metrics.failure_rate();
    if failure_rate > 5.0 {
        checks.push(HealthCheck {
            name: "failure_rate".to_string(),
            status: "unhealthy".to_string(),
            message: Some(format!("High failure rate: {:.2}%", failure_rate)),
        });
        overall_healthy = false;
    } else {
        checks.push(HealthCheck {
            name: "failure_rate".to_string(),
            status: "healthy".to_string(),
            message: None,
        });
    }

    let status = HealthStatus {
        status: if overall_healthy { "healthy".to_string() } else { "unhealthy".to_string() },
        mqtt_connected: last_message_age < 30,
        last_message_age_seconds: last_message_age,
        total_messages: metrics.messages_received,
        failure_rate,
        checks,
    };

    let status_code = if overall_healthy {
        StatusCode::OK
    } else {
        StatusCode::SERVICE_UNAVAILABLE
    };

    (status_code, Json(status))
}

async fn metrics_handler(
    State(state): State<Arc<AppState>>,
) -> String {
    state.monitoring.get_prometheus_metrics().await
}

pub async fn start_health_server(monitoring: Arc<MonitoringSystem>, port: u16) {
    let app_state = Arc::new(AppState { monitoring });

    let app = Router::new()
        .route("/health", get(health_handler))
        .route("/metrics", get(metrics_handler))
        .with_state(app_state);

    let addr = format!("0.0.0.0:{}", port);
    println!("Health check server listening on {}", addr);
    
    axum::Server::bind(&addr.parse().unwrap())
        .serve(app.into_make_service())
        .await
        .unwrap();
}
```

## Summary

**Monitoring and alerting for MQTT systems** involves tracking broker and client metrics, detecting anomalies, and responding to issues proactively. Key aspects include:

1. **Metrics Collection**: Track message counts, latency, connection status, failure rates, and system resources using atomic operations for thread safety.

2. **Multiple Approaches**: Implement push-based metrics (publishing to MQTT topics), pull-based (Prometheus endpoints), or hybrid systems combining both methods.

3. **Alert Strategies**: Use threshold-based alerts for immediate issues (connection loss, high failure rates), rate-of-change detection for sudden spikes, and composite conditions for complex scenarios.

4. **Integration Patterns**: Export metrics in Prometheus format for integration with Grafana, use HTTP endpoints for health checks, and publish alerts to MQTT topics for real-time notification systems.

5. **Production Best Practices**: Implement proper error handling, use non-blocking operations, keep recent metric history for trend analysis, and provide multiple interfaces (HTTP, MQTT) for accessing monitoring data.

Both C/C++ and Rust implementations demonstrate atomic operations for thread-safe metric updates, background monitoring tasks, threshold-based alerting, and standard metric formats for integration with existing monitoring infrastructure.