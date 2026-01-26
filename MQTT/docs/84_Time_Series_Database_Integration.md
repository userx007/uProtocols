# Time-Series Database Integration with MQTT

## Overview

Time-series database integration involves capturing MQTT messages and storing them in specialized databases optimized for time-stamped data. This enables efficient storage, querying, and analysis of sensor data, metrics, and events over time. Common time-series databases include InfluxDB, TimescaleDB, and Prometheus.

## Key Concepts

### Why Time-Series Databases?
- **Optimized Storage**: Compression and retention policies for time-stamped data
- **Fast Queries**: Efficient aggregation and downsampling operations
- **Scalability**: Handle millions of data points per second
- **Analysis**: Built-in functions for statistical analysis and trend detection

### Architecture Pattern
```
MQTT Broker → Subscriber → Time-Series DB → Visualization/Analytics
```

## C/C++ Implementation with InfluxDB

```c
#include <mosquitto.h>
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define INFLUX_URL "http://localhost:8086/api/v2/write?org=myorg&bucket=sensors&precision=s"
#define INFLUX_TOKEN "your-influx-token-here"
#define MQTT_BROKER "localhost"
#define MQTT_PORT 1883

typedef struct {
    CURL *curl;
    char *influx_url;
    char *auth_header;
} influx_ctx_t;

// Initialize InfluxDB context
influx_ctx_t* influx_init(const char *url, const char *token) {
    influx_ctx_t *ctx = malloc(sizeof(influx_ctx_t));
    ctx->curl = curl_easy_init();
    ctx->influx_url = strdup(url);
    
    // Create authorization header
    size_t header_len = strlen("Authorization: Token ") + strlen(token) + 1;
    ctx->auth_header = malloc(header_len);
    snprintf(ctx->auth_header, header_len, "Authorization: Token %s", token);
    
    return ctx;
}

// Write data to InfluxDB using Line Protocol
int influx_write(influx_ctx_t *ctx, const char *measurement, 
                 const char *tags, const char *fields) {
    char line_protocol[1024];
    time_t now = time(NULL);
    
    // Format: measurement,tag1=value1 field1=value1 timestamp
    snprintf(line_protocol, sizeof(line_protocol), "%s,%s %s %ld",
             measurement, tags, fields, now);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: text/plain");
    headers = curl_slist_append(headers, ctx->auth_header);
    
    curl_easy_setopt(ctx->curl, CURLOPT_URL, ctx->influx_url);
    curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDS, line_protocol);
    
    CURLcode res = curl_easy_perform(ctx->curl);
    curl_slist_free_all(headers);
    
    return (res == CURLE_OK) ? 0 : -1;
}

// MQTT message callback
void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    influx_ctx_t *ctx = (influx_ctx_t *)obj;
    
    // Parse topic: sensors/temperature/room1
    char measurement[64] = "sensor_data";
    char tags[256];
    char fields[256];
    
    // Extract location from topic
    char *topic_copy = strdup(msg->topic);
    char *token = strtok(topic_copy, "/");
    char *sensor_type = strtok(NULL, "/");
    char *location = strtok(NULL, "/");
    
    // Build tags and fields
    snprintf(tags, sizeof(tags), "sensor=%s,location=%s", 
             sensor_type ? sensor_type : "unknown",
             location ? location : "unknown");
    snprintf(fields, sizeof(fields), "value=%s", (char *)msg->payload);
    
    // Write to InfluxDB
    if (influx_write(ctx, measurement, tags, fields) == 0) {
        printf("Written to InfluxDB: %s\n", msg->topic);
    } else {
        fprintf(stderr, "Failed to write to InfluxDB\n");
    }
    
    free(topic_copy);
}

int main() {
    struct mosquitto *mosq;
    influx_ctx_t *influx_ctx;
    
    mosquitto_lib_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Initialize InfluxDB connection
    influx_ctx = influx_init(INFLUX_URL, INFLUX_TOKEN);
    
    // Initialize MQTT client
    mosq = mosquitto_new("influx_bridge", true, influx_ctx);
    mosquitto_message_callback_set(mosq, on_message);
    
    // Connect and subscribe
    if (mosquitto_connect(mosq, MQTT_BROKER, MQTT_PORT, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Unable to connect to MQTT broker\n");
        return 1;
    }
    
    mosquitto_subscribe(mosq, NULL, "sensors/#", 0);
    
    printf("MQTT to InfluxDB bridge started\n");
    mosquitto_loop_forever(mosq, -1, 1);
    
    // Cleanup
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    curl_easy_cleanup(influx_ctx->curl);
    curl_global_cleanup();
    free(influx_ctx->influx_url);
    free(influx_ctx->auth_header);
    free(influx_ctx);
    
    return 0;
}
```

## Rust Implementation with TimescaleDB

```rust
use rumqttc::{Client, Event, MqttOptions, Packet, QoS};
use tokio_postgres::{Client as PgClient, NoTls};
use serde_json::Value;
use chrono::Utc;
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Connect to TimescaleDB (PostgreSQL)
    let (db_client, connection) = tokio_postgres::connect(
        "host=localhost user=postgres password=password dbname=timeseries",
        NoTls,
    )
    .await?;

    // Spawn connection handler
    tokio::spawn(async move {
        if let Err(e) = connection.await {
            eprintln!("Connection error: {}", e);
        }
    });

    // Initialize TimescaleDB hypertable
    init_timescaledb(&db_client).await?;

    // Setup MQTT client
    let mut mqtt_options = MqttOptions::new("timescale_bridge", "localhost", 1883);
    mqtt_options.set_keep_alive(std::time::Duration::from_secs(30));

    let (client, mut eventloop) = Client::new(mqtt_options, 10);
    client.subscribe("sensors/#", QoS::AtLeastOnce)?;

    println!("MQTT to TimescaleDB bridge started");

    // Process MQTT messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                handle_mqtt_message(&db_client, &publish).await?;
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("MQTT error: {}", e);
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
            }
        }
    }
}

async fn init_timescaledb(client: &PgClient) -> Result<(), Box<dyn Error>> {
    // Create table
    client
        .execute(
            "CREATE TABLE IF NOT EXISTS sensor_data (
                time TIMESTAMPTZ NOT NULL,
                sensor_type TEXT NOT NULL,
                location TEXT NOT NULL,
                value DOUBLE PRECISION NOT NULL,
                metadata JSONB
            )",
            &[],
        )
        .await?;

    // Convert to hypertable (TimescaleDB specific)
    client
        .execute(
            "SELECT create_hypertable('sensor_data', 'time', 
             if_not_exists => TRUE)",
            &[],
        )
        .await?;

    // Create index for better query performance
    client
        .execute(
            "CREATE INDEX IF NOT EXISTS idx_sensor_location 
             ON sensor_data (sensor_type, location, time DESC)",
            &[],
        )
        .await?;

    println!("TimescaleDB initialized");
    Ok(())
}

async fn handle_mqtt_message(
    db_client: &PgClient,
    publish: &rumqttc::Publish,
) -> Result<(), Box<dyn Error>> {
    let topic = &publish.topic;
    let payload = std::str::from_utf8(&publish.payload)?;

    // Parse topic: sensors/temperature/room1
    let parts: Vec<&str> = topic.split('/').collect();
    if parts.len() < 3 {
        return Ok(());
    }

    let sensor_type = parts[1];
    let location = parts[2];

    // Parse payload (assume JSON or numeric value)
    let value: f64 = if let Ok(v) = payload.parse() {
        v
    } else if let Ok(json) = serde_json::from_str::<Value>(payload) {
        json["value"].as_f64().unwrap_or(0.0)
    } else {
        return Ok(());
    };

    // Insert into TimescaleDB
    db_client
        .execute(
            "INSERT INTO sensor_data (time, sensor_type, location, value, metadata) 
             VALUES ($1, $2, $3, $4, $5)",
            &[
                &Utc::now(),
                &sensor_type,
                &location,
                &value,
                &serde_json::json!({"topic": topic}),
            ],
        )
        .await?;

    println!("Stored: {} = {} ({})", topic, value, location);
    Ok(())
}
```

## Rust Implementation with Prometheus (Push Gateway)

```rust
use rumqttc::{Client, Event, MqttOptions, Packet, QoS};
use prometheus::{TextEncoder, Encoder, Gauge, Registry, Opts};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use reqwest;

type MetricsMap = Arc<Mutex<HashMap<String, Gauge>>>;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let registry = Registry::new();
    let metrics: MetricsMap = Arc::new(Mutex::new(HashMap::new()));

    // Setup MQTT client
    let mut mqtt_options = MqttOptions::new("prometheus_bridge", "localhost", 1883);
    mqtt_options.set_keep_alive(std::time::Duration::from_secs(30));

    let (client, mut eventloop) = Client::new(mqtt_options, 10);
    client.subscribe("sensors/#", QoS::AtLeastOnce)?;

    // Clone for async task
    let metrics_clone = Arc::clone(&metrics);
    let registry_clone = registry.clone();

    // Spawn task to push metrics to Prometheus Push Gateway
    tokio::spawn(async move {
        push_metrics_loop(metrics_clone, registry_clone).await;
    });

    println!("MQTT to Prometheus bridge started");

    // Process MQTT messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                handle_prometheus_message(&metrics, &registry, &publish).await?;
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("MQTT error: {}", e);
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
            }
        }
    }
}

async fn handle_prometheus_message(
    metrics: &MetricsMap,
    registry: &Registry,
    publish: &rumqttc::Publish,
) -> Result<(), Box<dyn std::error::Error>> {
    let topic = &publish.topic;
    let payload = std::str::from_utf8(&publish.payload)?;

    // Parse topic and value
    let parts: Vec<&str> = topic.split('/').collect();
    if parts.len() < 3 {
        return Ok(());
    }

    let sensor_type = parts[1];
    let location = parts[2];
    let metric_name = format!("mqtt_{}_{}", sensor_type, location);

    let value: f64 = payload.parse().unwrap_or(0.0);

    // Get or create gauge metric
    let mut metrics_map = metrics.lock().unwrap();
    let gauge = metrics_map.entry(metric_name.clone()).or_insert_with(|| {
        let opts = Opts::new(metric_name.clone(), "MQTT sensor value")
            .const_label("sensor", sensor_type)
            .const_label("location", location);
        let g = Gauge::with_opts(opts).unwrap();
        registry.register(Box::new(g.clone())).unwrap();
        g
    });

    gauge.set(value);
    println!("Updated metric: {} = {}", metric_name, value);

    Ok(())
}

async fn push_metrics_loop(metrics: MetricsMap, registry: Registry) {
    let client = reqwest::Client::new();
    let pushgateway_url = "http://localhost:9091/metrics/job/mqtt_bridge";

    loop {
        tokio::time::sleep(std::time::Duration::from_secs(10)).await;

        // Encode metrics
        let encoder = TextEncoder::new();
        let metric_families = registry.gather();
        let mut buffer = vec![];
        encoder.encode(&metric_families, &mut buffer).unwrap();

        // Push to Prometheus Push Gateway
        match client
            .post(pushgateway_url)
            .body(buffer)
            .send()
            .await
        {
            Ok(_) => println!("Metrics pushed to Prometheus"),
            Err(e) => eprintln!("Failed to push metrics: {}", e),
        }
    }
}
```

## Summary

**Time-Series Database Integration** with MQTT enables:

- **Persistent Storage**: Long-term retention of sensor data and metrics
- **Efficient Querying**: Fast aggregation, downsampling, and time-range queries
- **Scalability**: Handle high-frequency data from thousands of sensors
- **Analytics**: Statistical analysis, anomaly detection, and trend visualization
- **Retention Policies**: Automatic data expiration and downsampling for older data

**Key Implementation Patterns:**
- Subscribe to MQTT topics and parse messages
- Transform MQTT data into database-specific formats (Line Protocol for InfluxDB, SQL for TimescaleDB, Prometheus metrics)
- Handle connection failures and implement retry logic
- Use batch inserts for high-throughput scenarios
- Implement proper indexing and retention policies

**Database Selection:**
- **InfluxDB**: Purpose-built for time-series, easy setup, good for IoT
- **TimescaleDB**: PostgreSQL extension, SQL familiarity, relational capabilities
- **Prometheus**: Metrics-focused, pull model with push gateway support, excellent for monitoring