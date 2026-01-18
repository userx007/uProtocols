# Message Batching in MQTT

## Overview

Message batching is an optimization technique that combines multiple MQTT publish operations into grouped transmissions, reducing protocol overhead and improving overall throughput. Instead of sending messages individually as they're generated, batching accumulates messages over a time window or count threshold before transmitting them together.

## Why Message Batching Matters

### Benefits
- **Reduced Protocol Overhead**: Fewer TCP packets and MQTT frame headers
- **Improved Throughput**: Higher effective data transfer rates
- **Network Efficiency**: Better utilization of available bandwidth
- **Lower Latency Variance**: More predictable message delivery patterns
- **Resource Optimization**: Reduced CPU cycles for packet processing

### Trade-offs
- **Increased Latency**: Messages wait in batch before transmission
- **Memory Usage**: Requires buffering messages
- **Complexity**: Additional logic for batch management
- **QoS Considerations**: Must maintain delivery guarantees

## Batching Strategies

### 1. **Time-based Batching**
Collect messages for a fixed duration before publishing.

### 2. **Count-based Batching**
Publish when a specific number of messages accumulates.

### 3. **Size-based Batching**
Trigger publication when total payload reaches a threshold.

### 4. **Hybrid Batching**
Combine multiple strategies (e.g., "publish every 100ms OR 50 messages").

## Implementation Examples

### C/C++ Implementation with Paho MQTT

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "MQTTClient.h"

#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID "BatchPublisher"
#define BATCH_SIZE 10
#define BATCH_TIMEOUT_MS 1000
#define QOS 1

typedef struct {
    char topic[256];
    char payload[1024];
    int qos;
} BatchMessage;

typedef struct {
    BatchMessage messages[BATCH_SIZE];
    int count;
    time_t last_flush;
    MQTTClient client;
} MessageBatcher;

// Initialize the batcher
void batcher_init(MessageBatcher* batcher, MQTTClient client) {
    batcher->count = 0;
    batcher->last_flush = time(NULL);
    batcher->client = client;
    memset(batcher->messages, 0, sizeof(batcher->messages));
}

// Add message to batch
int batcher_add(MessageBatcher* batcher, const char* topic, 
                const char* payload, int qos) {
    if (batcher->count >= BATCH_SIZE) {
        return -1; // Batch full
    }
    
    strncpy(batcher->messages[batcher->count].topic, topic, 255);
    strncpy(batcher->messages[batcher->count].payload, payload, 1023);
    batcher->messages[batcher->count].qos = qos;
    batcher->count++;
    
    return 0;
}

// Flush batch to MQTT broker
int batcher_flush(MessageBatcher* batcher) {
    if (batcher->count == 0) {
        return 0; // Nothing to flush
    }
    
    printf("Flushing batch of %d messages\n", batcher->count);
    
    for (int i = 0; i < batcher->count; i++) {
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;
        
        pubmsg.payload = batcher->messages[i].payload;
        pubmsg.payloadlen = strlen(batcher->messages[i].payload);
        pubmsg.qos = batcher->messages[i].qos;
        pubmsg.retained = 0;
        
        int rc = MQTTClient_publishMessage(batcher->client, 
                                           batcher->messages[i].topic,
                                           &pubmsg, &token);
        
        if (rc != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "Failed to publish message %d: %d\n", i, rc);
            return rc;
        }
        
        // For QoS > 0, wait for delivery
        if (batcher->messages[i].qos > 0) {
            MQTTClient_waitForCompletion(batcher->client, token, 5000);
        }
    }
    
    batcher->count = 0;
    batcher->last_flush = time(NULL);
    return 0;
}

// Check if batch should be flushed (time-based or count-based)
int batcher_should_flush(MessageBatcher* batcher) {
    time_t now = time(NULL);
    long elapsed_ms = (now - batcher->last_flush) * 1000;
    
    return (batcher->count >= BATCH_SIZE) || 
           (elapsed_ms >= BATCH_TIMEOUT_MS && batcher->count > 0);
}

// JSON batching example - combine multiple messages into one
char* create_batched_json(MessageBatcher* batcher) {
    static char json_buffer[8192];
    int offset = 0;
    
    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset,
                      "{\"messages\":[");
    
    for (int i = 0; i < batcher->count; i++) {
        if (i > 0) {
            offset += snprintf(json_buffer + offset, 
                             sizeof(json_buffer) - offset, ",");
        }
        offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset,
                          "{\"topic\":\"%s\",\"data\":%s}",
                          batcher->messages[i].topic,
                          batcher->messages[i].payload);
    }
    
    snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "]}");
    return json_buffer;
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                     MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Failed to connect\n");
        return EXIT_FAILURE;
    }
    
    MessageBatcher batcher;
    batcher_init(&batcher, client);
    
    // Simulate sensor data generation
    for (int i = 0; i < 25; i++) {
        char payload[256];
        snprintf(payload, sizeof(payload), 
                "{\"sensor_id\":%d,\"temperature\":%.2f,\"timestamp\":%ld}",
                i, 20.0 + (i % 10), time(NULL));
        
        batcher_add(&batcher, "sensors/temperature", payload, QOS);
        
        // Check if we should flush
        if (batcher_should_flush(&batcher)) {
            batcher_flush(&batcher);
        }
        
        usleep(50000); // 50ms delay between readings
    }
    
    // Flush any remaining messages
    batcher_flush(&batcher);
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return EXIT_SUCCESS;
}
```

### Rust Implementation with rumqttc

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS};
use serde::{Deserialize, Serialize};
use std::time::{Duration, Instant};
use tokio::time::sleep;

const BATCH_SIZE: usize = 10;
const BATCH_TIMEOUT: Duration = Duration::from_millis(1000);

#[derive(Debug, Clone, Serialize, Deserialize)]
struct SensorReading {
    sensor_id: u32,
    temperature: f64,
    timestamp: u64,
}

#[derive(Debug, Clone)]
struct BatchedMessage {
    topic: String,
    payload: String,
    qos: QoS,
}

struct MessageBatcher {
    messages: Vec<BatchedMessage>,
    last_flush: Instant,
    client: AsyncClient,
}

impl MessageBatcher {
    fn new(client: AsyncClient) -> Self {
        Self {
            messages: Vec::with_capacity(BATCH_SIZE),
            last_flush: Instant::now(),
            client,
        }
    }

    fn add(&mut self, topic: String, payload: String, qos: QoS) {
        self.messages.push(BatchedMessage {
            topic,
            payload,
            qos,
        });
    }

    async fn flush(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if self.messages.is_empty() {
            return Ok(());
        }

        println!("Flushing batch of {} messages", self.messages.len());

        for msg in &self.messages {
            self.client
                .publish(&msg.topic, msg.qos, false, msg.payload.as_bytes())
                .await?;
        }

        self.messages.clear();
        self.last_flush = Instant::now();
        Ok(())
    }

    fn should_flush(&self) -> bool {
        self.messages.len() >= BATCH_SIZE
            || (self.last_flush.elapsed() >= BATCH_TIMEOUT && !self.messages.is_empty())
    }

    // Combine multiple messages into a single JSON array
    fn create_combined_payload(&self) -> Result<String, serde_json::Error> {
        #[derive(Serialize)]
        struct BatchPayload {
            messages: Vec<serde_json::Value>,
        }

        let messages: Vec<serde_json::Value> = self
            .messages
            .iter()
            .filter_map(|msg| serde_json::from_str(&msg.payload).ok())
            .collect();

        serde_json::to_string(&BatchPayload { messages })
    }

    async fn flush_combined(&mut self, combined_topic: &str) -> Result<(), Box<dyn std::error::Error>> {
        if self.messages.is_empty() {
            return Ok(());
        }

        let combined_payload = self.create_combined_payload()?;
        
        self.client
            .publish(combined_topic, QoS::AtLeastOnce, false, combined_payload.as_bytes())
            .await?;

        println!("Flushed combined batch with {} messages", self.messages.len());
        self.messages.clear();
        self.last_flush = Instant::now();
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("batch-publisher", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn event loop handler
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Event loop error: {:?}", e);
                    break;
                }
            }
        }
    });

    // Wait for connection
    sleep(Duration::from_millis(500)).await;

    let mut batcher = MessageBatcher::new(client);

    // Simulate sensor data generation
    for i in 0..25 {
        let reading = SensorReading {
            sensor_id: i,
            temperature: 20.0 + (i % 10) as f64,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?
                .as_secs(),
        };

        let payload = serde_json::to_string(&reading)?;
        batcher.add(
            "sensors/temperature".to_string(),
            payload,
            QoS::AtLeastOnce,
        );

        // Check if we should flush
        if batcher.should_flush() {
            batcher.flush().await?;
        }

        sleep(Duration::from_millis(50)).await;
    }

    // Flush any remaining messages
    batcher.flush().await?;

    sleep(Duration::from_secs(1)).await;
    Ok(())
}
```

### Advanced Rust Example with Channels

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS};
use tokio::sync::mpsc;
use tokio::time::{interval, Duration};

struct BatchPublisher {
    tx: mpsc::Sender<BatchedMessage>,
}

impl BatchPublisher {
    fn new(client: AsyncClient) -> Self {
        let (tx, rx) = mpsc::channel(100);
        
        tokio::spawn(Self::batch_worker(client, rx));
        
        Self { tx }
    }

    async fn publish(&self, topic: String, payload: String, qos: QoS) -> Result<(), mpsc::error::SendError<BatchedMessage>> {
        self.tx.send(BatchedMessage { topic, payload, qos }).await
    }

    async fn batch_worker(client: AsyncClient, mut rx: mpsc::Receiver<BatchedMessage>) {
        let mut batch = Vec::with_capacity(BATCH_SIZE);
        let mut ticker = interval(BATCH_TIMEOUT);

        loop {
            tokio::select! {
                msg = rx.recv() => {
                    match msg {
                        Some(msg) => {
                            batch.push(msg);
                            if batch.len() >= BATCH_SIZE {
                                Self::flush_batch(&client, &mut batch).await;
                            }
                        }
                        None => {
                            // Channel closed, flush and exit
                            Self::flush_batch(&client, &mut batch).await;
                            break;
                        }
                    }
                }
                _ = ticker.tick() => {
                    if !batch.is_empty() {
                        Self::flush_batch(&client, &mut batch).await;
                    }
                }
            }
        }
    }

    async fn flush_batch(client: &AsyncClient, batch: &mut Vec<BatchedMessage>) {
        println!("Flushing batch of {} messages", batch.len());
        
        for msg in batch.drain(..) {
            if let Err(e) = client.publish(&msg.topic, msg.qos, false, msg.payload.as_bytes()).await {
                eprintln!("Failed to publish: {:?}", e);
            }
        }
    }
}
```

## Best Practices

1. **Choose Appropriate Batch Size**: Balance between latency and throughput based on your use case
2. **Monitor Memory Usage**: Implement size limits to prevent unbounded growth
3. **Handle Failures Gracefully**: Implement retry logic for failed batches
4. **Consider QoS Implications**: Higher QoS levels may negate batching benefits
5. **Use Timeouts**: Always implement time-based flushing to prevent indefinite delays
6. **Profile Performance**: Measure actual throughput improvements in your environment
7. **Combine Messages Intelligently**: For related data, consider combining into single payloads

## Summary

Message batching is a powerful optimization technique for MQTT applications requiring high throughput. By accumulating messages before transmission, you reduce protocol overhead and improve network efficiency. The implementations shown demonstrate both individual message batching (sending multiple discrete messages together) and payload combination (merging multiple messages into single payloads). The key is balancing latency requirements against throughput gains, using hybrid strategies with both count and time-based triggers. Proper implementation requires careful memory management, error handling, and consideration of QoS guarantees to maintain MQTT's reliability while achieving performance benefits.