# Stream Processing with MQTT

## Overview

Stream processing with MQTT combines the lightweight, real-time messaging capabilities of MQTT with powerful stream processing frameworks like Apache Kafka, Apache Flink, or Apache Spark. This architecture enables continuous analysis, transformation, and aggregation of IoT data streams as they flow through the system, allowing for real-time insights, anomaly detection, and automated responses.

## Architecture Pattern

The typical architecture involves:
1. **MQTT Brokers** - Collect data from IoT devices
2. **Bridge/Connector** - Transfers MQTT messages to stream processors
3. **Stream Processing Engine** - Processes data in real-time (windowing, aggregation, filtering)
4. **Data Sinks** - Stores results in databases, triggers alerts, or publishes to output MQTT topics

## Key Concepts

- **Stateful Processing**: Maintain context across events (counters, averages, patterns)
- **Windowing**: Process data in time-based or count-based windows
- **Event Time vs Processing Time**: Handle out-of-order events correctly
- **Exactly-Once Semantics**: Ensure data integrity in distributed processing
- **Back Pressure**: Handle varying data rates gracefully

---

## C/C++ Implementation

Using Eclipse Paho MQTT with in-memory stream processing:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>
#include <time.h>
#include <pthread.h>

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "StreamProcessor"
#define TOPIC       "sensors/+/temperature"
#define QOS         1

// Sliding window for stream processing
typedef struct {
    double values[100];
    time_t timestamps[100];
    int count;
    int head;
    pthread_mutex_t lock;
} SlidingWindow;

typedef struct {
    char sensor_id[64];
    SlidingWindow window;
} SensorStream;

SensorStream streams[10];
int stream_count = 0;
pthread_mutex_t streams_lock = PTHREAD_MUTEX_INITIALIZER;

void init_window(SlidingWindow *window) {
    window->count = 0;
    window->head = 0;
    pthread_mutex_init(&window->lock, NULL);
}

// Add value to sliding window (1-minute window)
void add_to_window(SlidingWindow *window, double value) {
    pthread_mutex_lock(&window->lock);
    
    time_t now = time(NULL);
    
    // Remove old values (older than 60 seconds)
    for (int i = 0; i < window->count; i++) {
        int idx = (window->head + i) % 100;
        if (now - window->timestamps[idx] > 60) {
            window->head = (window->head + 1) % 100;
            window->count--;
        } else {
            break;
        }
    }
    
    // Add new value
    if (window->count < 100) {
        int idx = (window->head + window->count) % 100;
        window->values[idx] = value;
        window->timestamps[idx] = now;
        window->count++;
    }
    
    pthread_mutex_unlock(&window->lock);
}

// Calculate moving average
double calculate_average(SlidingWindow *window) {
    pthread_mutex_lock(&window->lock);
    
    if (window->count == 0) {
        pthread_mutex_unlock(&window->lock);
        return 0.0;
    }
    
    double sum = 0.0;
    for (int i = 0; i < window->count; i++) {
        int idx = (window->head + i) % 100;
        sum += window->values[idx];
    }
    
    double avg = sum / window->count;
    pthread_mutex_unlock(&window->lock);
    
    return avg;
}

// Detect anomalies (simple threshold-based)
int detect_anomaly(SlidingWindow *window, double current_value) {
    double avg = calculate_average(window);
    double threshold = 5.0; // Temperature deviation threshold
    
    if (window->count > 5 && fabs(current_value - avg) > threshold) {
        return 1;
    }
    return 0;
}

SensorStream* get_or_create_stream(const char *sensor_id) {
    pthread_mutex_lock(&streams_lock);
    
    // Find existing stream
    for (int i = 0; i < stream_count; i++) {
        if (strcmp(streams[i].sensor_id, sensor_id) == 0) {
            pthread_mutex_unlock(&streams_lock);
            return &streams[i];
        }
    }
    
    // Create new stream
    if (stream_count < 10) {
        strncpy(streams[stream_count].sensor_id, sensor_id, 63);
        init_window(&streams[stream_count].window);
        stream_count++;
        pthread_mutex_unlock(&streams_lock);
        return &streams[stream_count - 1];
    }
    
    pthread_mutex_unlock(&streams_lock);
    return NULL;
}

int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    // Parse topic to extract sensor ID
    char sensor_id[64];
    char *token = strtok(topicName, "/");
    token = strtok(NULL, "/"); // Get sensor ID
    if (token) {
        strncpy(sensor_id, token, 63);
        sensor_id[63] = '\0';
    }
    
    // Parse temperature value
    double temperature = atof((char*)message->payload);
    
    // Get or create stream for this sensor
    SensorStream *stream = get_or_create_stream(sensor_id);
    if (stream) {
        // Add to window
        add_to_window(&stream->window, temperature);
        
        // Calculate metrics
        double avg = calculate_average(&stream->window);
        
        // Detect anomaly
        if (detect_anomaly(&stream->window, temperature)) {
            printf("[ANOMALY] Sensor %s: %.2f°C (avg: %.2f°C, count: %d)\n",
                   sensor_id, temperature, avg, stream->window.count);
        } else {
            printf("[NORMAL] Sensor %s: %.2f°C (avg: %.2f°C, count: %d)\n",
                   sensor_id, temperature, avg, stream->window.count);
        }
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connection_lost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Stream processor connected. Subscribing to %s\n", TOPIC);
    MQTTClient_subscribe(client, TOPIC, QOS);
    
    printf("Processing streams... Press Ctrl+C to exit\n");
    
    // Keep running
    while (1) {
        sleep(1);
    }
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return 0;
}
```

---

## Rust Implementation

Using `rumqttc` with Tokio for async stream processing:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{Duration, Instant};
use std::collections::{HashMap, VecDeque};
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Clone, Debug)]
struct DataPoint {
    value: f64,
    timestamp: Instant,
}

#[derive(Debug)]
struct SlidingWindow {
    values: VecDeque<DataPoint>,
    window_duration: Duration,
}

impl SlidingWindow {
    fn new(window_duration: Duration) -> Self {
        SlidingWindow {
            values: VecDeque::new(),
            window_duration,
        }
    }
    
    fn add(&mut self, value: f64) {
        let now = Instant::now();
        
        // Remove old values
        while let Some(front) = self.values.front() {
            if now.duration_since(front.timestamp) > self.window_duration {
                self.values.pop_front();
            } else {
                break;
            }
        }
        
        // Add new value
        self.values.push_back(DataPoint {
            value,
            timestamp: now,
        });
    }
    
    fn average(&self) -> Option<f64> {
        if self.values.is_empty() {
            return None;
        }
        
        let sum: f64 = self.values.iter().map(|dp| dp.value).sum();
        Some(sum / self.values.len() as f64)
    }
    
    fn count(&self) -> usize {
        self.values.len()
    }
    
    fn min_max(&self) -> Option<(f64, f64)> {
        if self.values.is_empty() {
            return None;
        }
        
        let mut min = f64::MAX;
        let mut max = f64::MIN;
        
        for dp in &self.values {
            if dp.value < min {
                min = dp.value;
            }
            if dp.value > max {
                max = dp.value;
            }
        }
        
        Some((min, max))
    }
}

type StreamMap = Arc<Mutex<HashMap<String, SlidingWindow>>>;

async fn process_message(
    streams: StreamMap,
    sensor_id: String,
    temperature: f64,
) {
    let mut streams_lock = streams.lock().await;
    
    let window = streams_lock
        .entry(sensor_id.clone())
        .or_insert_with(|| SlidingWindow::new(Duration::from_secs(60)));
    
    window.add(temperature);
    
    // Calculate metrics
    let avg = window.average().unwrap_or(0.0);
    let count = window.count();
    let (min, max) = window.min_max().unwrap_or((0.0, 0.0));
    
    // Anomaly detection
    let threshold = 5.0;
    let is_anomaly = count > 5 && (temperature - avg).abs() > threshold;
    
    if is_anomaly {
        println!(
            "[ANOMALY] Sensor {}: {:.2}°C (avg: {:.2}°C, min: {:.2}°C, max: {:.2}°C, count: {})",
            sensor_id, temperature, avg, min, max, count
        );
    } else {
        println!(
            "[NORMAL] Sensor {}: {:.2}°C (avg: {:.2}°C, min: {:.2}°C, max: {:.2}°C, count: {})",
            sensor_id, temperature, avg, min, max, count
        );
    }
}

// Advanced: Tumbling window aggregation
struct TumblingWindowAggregator {
    window_size: Duration,
    current_window: Vec<f64>,
    window_start: Instant,
}

impl TumblingWindowAggregator {
    fn new(window_size: Duration) -> Self {
        TumblingWindowAggregator {
            window_size,
            current_window: Vec::new(),
            window_start: Instant::now(),
        }
    }
    
    fn add(&mut self, value: f64) -> Option<WindowResult> {
        let now = Instant::now();
        
        // Check if window should close
        if now.duration_since(self.window_start) >= self.window_size {
            let result = self.compute_result();
            
            // Start new window
            self.current_window.clear();
            self.window_start = now;
            self.current_window.push(value);
            
            return Some(result);
        }
        
        self.current_window.push(value);
        None
    }
    
    fn compute_result(&self) -> WindowResult {
        let count = self.current_window.len();
        let sum: f64 = self.current_window.iter().sum();
        let avg = if count > 0 { sum / count as f64 } else { 0.0 };
        
        WindowResult {
            count,
            sum,
            average: avg,
        }
    }
}

#[derive(Debug)]
struct WindowResult {
    count: usize,
    sum: f64,
    average: f64,
}

#[tokio::main]
async fn main() {
    let streams: StreamMap = Arc::new(Mutex::new(HashMap::new()));
    
    let mut mqttoptions = MqttOptions::new("stream_processor", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    client
        .subscribe("sensors/+/temperature", QoS::AtLeastOnce)
        .await
        .unwrap();
    
    println!("Stream processor started. Processing events...");
    
    while let Ok(event) = eventloop.poll().await {
        if let Event::Incoming(Packet::Publish(publish)) = event {
            let topic = publish.topic.clone();
            let payload = String::from_utf8_lossy(&publish.payload);
            
            // Parse sensor ID from topic
            let parts: Vec<&str> = topic.split('/').collect();
            if parts.len() >= 3 {
                let sensor_id = parts[1].to_string();
                
                // Parse temperature
                if let Ok(temperature) = payload.trim().parse::<f64>() {
                    let streams_clone = streams.clone();
                    
                    tokio::spawn(async move {
                        process_message(streams_clone, sensor_id, temperature).await;
                    });
                }
            }
        }
    }
}
```

---

## Summary

**Stream Processing with MQTT** enables real-time analytics on IoT data by combining MQTT's efficient messaging with powerful stream processing capabilities. The pattern supports:

- **Windowing Operations**: Time-based (sliding, tumbling, session) windows for aggregations
- **Stateful Processing**: Maintaining context across events for pattern detection
- **Real-time Analytics**: Immediate calculation of averages, minimums, maximums, and trends
- **Anomaly Detection**: Identifying outliers and unusual patterns as they occur
- **Scalability**: Processing thousands of events per second across multiple sensors

The C/C++ example demonstrates a basic in-memory sliding window implementation with multi-threaded processing, suitable for embedded systems or edge devices. The Rust example leverages async programming for efficient concurrent stream processing with more sophisticated window management.

For production systems, integration with Apache Kafka (for durable message queuing), Apache Flink (for complex event processing), or Apache Spark Streaming (for batch + stream processing) provides enterprise-grade capabilities including fault tolerance, exactly-once processing, and distributed computing across clusters.