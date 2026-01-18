# Load Testing MQTT Systems

Load testing MQTT systems is a critical practice for ensuring your message broker and client applications can handle expected (and unexpected) traffic volumes. This involves simulating multiple concurrent clients, measuring performance metrics, and identifying bottlenecks before they impact production systems.

## Why Load Test MQTT Systems?

MQTT brokers must handle:
- **Thousands of concurrent connections** from IoT devices
- **High message throughput** with minimal latency
- **Variable message sizes** and QoS levels
- **Connection storms** during network issues or system restarts
- **Retained messages** and persistent sessions

Load testing helps you:
- Determine maximum concurrent connections your broker can handle
- Measure message latency under various loads
- Identify memory leaks and resource exhaustion
- Validate your infrastructure scaling strategy
- Establish performance baselines

## Key Metrics to Monitor

When load testing MQTT systems, track these metrics:

- **Connection Rate**: Connections per second the broker can accept
- **Message Throughput**: Messages per second (publish/subscribe)
- **Message Latency**: Time from publish to delivery
- **CPU and Memory Usage**: Resource consumption patterns
- **Connection Failures**: Failed connection attempts
- **Message Loss**: Dropped messages under load
- **Network Bandwidth**: Data transfer rates

## Load Testing Strategies

**Ramp-Up Testing**: Gradually increase the number of clients to find the breaking point.

**Sustained Load Testing**: Maintain a constant load over extended periods to detect memory leaks.

**Spike Testing**: Sudden traffic bursts to simulate connection storms.

**Stress Testing**: Push the system beyond normal operational capacity.

## C/C++ Load Testing Example

Using the Eclipse Paho MQTT C library, here's a multi-threaded load test client:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "MQTTClient.h"

#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID_PREFIX "load_test_client_"
#define TOPIC "test/load"
#define QOS 1
#define TIMEOUT 10000L

typedef struct {
    int client_id;
    int num_messages;
    int message_size;
    long *latencies;
    int success_count;
    int failure_count;
} ClientStats;

typedef struct {
    int client_id;
    int num_messages;
    int message_size;
    int publish_interval_ms;
} ClientConfig;

// Thread function for each simulated client
void* mqtt_client_thread(void* arg) {
    ClientConfig* config = (ClientConfig*)arg;
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    char client_id[64];
    snprintf(client_id, sizeof(client_id), "%s%d", CLIENT_ID_PREFIX, config->client_id);
    
    ClientStats* stats = malloc(sizeof(ClientStats));
    stats->client_id = config->client_id;
    stats->num_messages = config->num_messages;
    stats->latencies = calloc(config->num_messages, sizeof(long));
    stats->success_count = 0;
    stats->failure_count = 0;
    
    // Create client
    int rc = MQTTClient_create(&client, BROKER_ADDRESS, client_id,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Client %d: Failed to create client, error %d\n", 
                config->client_id, rc);
        free(stats->latencies);
        free(stats);
        free(config);
        return NULL;
    }
    
    // Connect options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.connectTimeout = 5;
    
    // Connect to broker
    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Client %d: Failed to connect, error %d\n", 
                config->client_id, rc);
        MQTTClient_destroy(&client);
        free(stats->latencies);
        free(stats);
        free(config);
        return NULL;
    }
    
    printf("Client %d: Connected successfully\n", config->client_id);
    
    // Prepare message payload
    char* payload = malloc(config->message_size);
    memset(payload, 'A', config->message_size);
    payload[config->message_size - 1] = '\0';
    
    // Publish messages
    for (int i = 0; i < config->num_messages; i++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        pubmsg.payload = payload;
        pubmsg.payloadlen = config->message_size;
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        
        rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        
        if (rc == MQTTCLIENT_SUCCESS) {
            rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
            clock_gettime(CLOCK_MONOTONIC, &end);
            
            if (rc == MQTTCLIENT_SUCCESS) {
                long latency_us = (end.tv_sec - start.tv_sec) * 1000000L + 
                                  (end.tv_nsec - start.tv_nsec) / 1000L;
                stats->latencies[i] = latency_us;
                stats->success_count++;
            } else {
                stats->failure_count++;
            }
        } else {
            stats->failure_count++;
        }
        
        // Throttle publishing rate
        if (config->publish_interval_ms > 0) {
            usleep(config->publish_interval_ms * 1000);
        }
    }
    
    // Cleanup
    free(payload);
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
    
    printf("Client %d: Completed - Success: %d, Failures: %d\n",
           config->client_id, stats->success_count, stats->failure_count);
    
    free(config);
    return stats;
}

// Calculate statistics from latency data
void calculate_stats(ClientStats** all_stats, int num_clients) {
    long total_messages = 0;
    long total_success = 0;
    long total_failures = 0;
    long min_latency = LONG_MAX;
    long max_latency = 0;
    long sum_latency = 0;
    
    // Collect all latencies for percentile calculation
    long* all_latencies = NULL;
    int latency_count = 0;
    
    for (int i = 0; i < num_clients; i++) {
        if (all_stats[i] != NULL) {
            total_success += all_stats[i]->success_count;
            total_failures += all_stats[i]->failure_count;
            
            for (int j = 0; j < all_stats[i]->success_count; j++) {
                long lat = all_stats[i]->latencies[j];
                sum_latency += lat;
                if (lat < min_latency) min_latency = lat;
                if (lat > max_latency) max_latency = lat;
                latency_count++;
            }
        }
    }
    
    printf("\n=== Load Test Results ===\n");
    printf("Total Clients: %d\n", num_clients);
    printf("Total Messages Sent: %ld\n", total_success + total_failures);
    printf("Successful: %ld\n", total_success);
    printf("Failed: %ld\n", total_failures);
    printf("Success Rate: %.2f%%\n", 
           (total_success * 100.0) / (total_success + total_failures));
    
    if (latency_count > 0) {
        printf("\nLatency Statistics (microseconds):\n");
        printf("  Min: %ld µs\n", min_latency);
        printf("  Max: %ld µs\n", max_latency);
        printf("  Avg: %ld µs\n", sum_latency / latency_count);
    }
}

int main(int argc, char* argv[]) {
    int num_clients = 100;
    int messages_per_client = 100;
    int message_size = 128;
    int publish_interval_ms = 10;
    
    if (argc > 1) num_clients = atoi(argv[1]);
    if (argc > 2) messages_per_client = atoi(argv[2]);
    if (argc > 3) message_size = atoi(argv[3]);
    
    printf("Starting MQTT Load Test\n");
    printf("Clients: %d, Messages/Client: %d, Message Size: %d bytes\n",
           num_clients, messages_per_client, message_size);
    
    pthread_t* threads = malloc(num_clients * sizeof(pthread_t));
    ClientStats** stats = malloc(num_clients * sizeof(ClientStats*));
    
    struct timespec test_start, test_end;
    clock_gettime(CLOCK_MONOTONIC, &test_start);
    
    // Create all client threads
    for (int i = 0; i < num_clients; i++) {
        ClientConfig* config = malloc(sizeof(ClientConfig));
        config->client_id = i;
        config->num_messages = messages_per_client;
        config->message_size = message_size;
        config->publish_interval_ms = publish_interval_ms;
        
        pthread_create(&threads[i], NULL, mqtt_client_thread, config);
        
        // Stagger client creation to avoid connection storm
        usleep(10000); // 10ms delay between client starts
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < num_clients; i++) {
        pthread_join(threads[i], (void**)&stats[i]);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &test_end);
    long total_time_ms = (test_end.tv_sec - test_start.tv_sec) * 1000L + 
                         (test_end.tv_nsec - test_start.tv_nsec) / 1000000L;
    
    calculate_stats(stats, num_clients);
    printf("\nTotal Test Duration: %ld ms\n", total_time_ms);
    printf("Overall Throughput: %.2f messages/sec\n",
           (num_clients * messages_per_client * 1000.0) / total_time_ms);
    
    // Cleanup
    for (int i = 0; i < num_clients; i++) {
        if (stats[i] != NULL) {
            free(stats[i]->latencies);
            free(stats[i]);
        }
    }
    free(stats);
    free(threads);
    
    return 0;
}
```

## Rust Load Testing Example

Using the `paho-mqtt` and `tokio` crates for async load testing:

```rust
use paho_mqtt as mqtt;
use std::sync::{Arc, atomic::{AtomicU64, Ordering}};
use std::time::{Duration, Instant};
use tokio::task::JoinHandle;
use tokio::time::sleep;

const BROKER_URL: &str = "tcp://localhost:1883";
const TOPIC: &str = "test/load";
const QOS: i32 = 1;

#[derive(Clone)]
struct LoadTestConfig {
    num_clients: usize,
    messages_per_client: usize,
    message_size: usize,
    publish_interval_ms: u64,
    connection_timeout: Duration,
}

struct TestMetrics {
    successful_messages: AtomicU64,
    failed_messages: AtomicU64,
    total_latency_us: AtomicU64,
    min_latency_us: AtomicU64,
    max_latency_us: AtomicU64,
}

impl TestMetrics {
    fn new() -> Self {
        TestMetrics {
            successful_messages: AtomicU64::new(0),
            failed_messages: AtomicU64::new(0),
            total_latency_us: AtomicU64::new(0),
            min_latency_us: AtomicU64::new(u64::MAX),
            max_latency_us: AtomicU64::new(0),
        }
    }
    
    fn record_success(&self, latency_us: u64) {
        self.successful_messages.fetch_add(1, Ordering::Relaxed);
        self.total_latency_us.fetch_add(latency_us, Ordering::Relaxed);
        
        // Update min latency
        let mut current_min = self.min_latency_us.load(Ordering::Relaxed);
        while latency_us < current_min {
            match self.min_latency_us.compare_exchange_weak(
                current_min,
                latency_us,
                Ordering::Relaxed,
                Ordering::Relaxed
            ) {
                Ok(_) => break,
                Err(x) => current_min = x,
            }
        }
        
        // Update max latency
        let mut current_max = self.max_latency_us.load(Ordering::Relaxed);
        while latency_us > current_max {
            match self.max_latency_us.compare_exchange_weak(
                current_max,
                latency_us,
                Ordering::Relaxed,
                Ordering::Relaxed
            ) {
                Ok(_) => break,
                Err(x) => current_max = x,
            }
        }
    }
    
    fn record_failure(&self) {
        self.failed_messages.fetch_add(1, Ordering::Relaxed);
    }
    
    fn print_summary(&self, duration: Duration, num_clients: usize) {
        let success = self.successful_messages.load(Ordering::Relaxed);
        let failures = self.failed_messages.load(Ordering::Relaxed);
        let total = success + failures;
        let total_latency = self.total_latency_us.load(Ordering::Relaxed);
        
        println!("\n=== Load Test Results ===");
        println!("Total Clients: {}", num_clients);
        println!("Total Messages: {}", total);
        println!("Successful: {}", success);
        println!("Failed: {}", failures);
        println!("Success Rate: {:.2}%", (success as f64 / total as f64) * 100.0);
        println!("\nLatency Statistics (microseconds):");
        println!("  Min: {} µs", self.min_latency_us.load(Ordering::Relaxed));
        println!("  Max: {} µs", self.max_latency_us.load(Ordering::Relaxed));
        
        if success > 0 {
            println!("  Avg: {} µs", total_latency / success);
        }
        
        let duration_secs = duration.as_secs_f64();
        println!("\nTotal Duration: {:.2} seconds", duration_secs);
        println!("Throughput: {:.2} messages/sec", total as f64 / duration_secs);
        println!("Throughput per client: {:.2} messages/sec", 
                 total as f64 / duration_secs / num_clients as f64);
    }
}

async fn mqtt_client_task(
    client_id: usize,
    config: LoadTestConfig,
    metrics: Arc<TestMetrics>,
) -> Result<(), Box<dyn std::error::Error>> {
    // Create client
    let client_name = format!("load_test_client_{}", client_id);
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri(BROKER_URL)
        .client_id(client_name)
        .finalize();
    
    let client = mqtt::AsyncClient::new(create_opts)?;
    
    // Connection options
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .connect_timeout(config.connection_timeout)
        .finalize();
    
    // Connect to broker
    match client.connect(conn_opts).await {
        Ok(_) => println!("Client {}: Connected", client_id),
        Err(e) => {
            eprintln!("Client {}: Connection failed: {}", client_id, e);
            return Err(Box::new(e));
        }
    }
    
    // Prepare payload
    let payload = "A".repeat(config.message_size);
    
    // Publish messages
    for msg_num in 0..config.messages_per_client {
        let start = Instant::now();
        
        let msg = mqtt::MessageBuilder::new()
            .topic(TOPIC)
            .payload(payload.clone())
            .qos(QOS)
            .finalize();
        
        match client.publish(msg).await {
            Ok(_) => {
                let latency = start.elapsed().as_micros() as u64;
                metrics.record_success(latency);
            }
            Err(e) => {
                eprintln!("Client {}: Publish {} failed: {}", client_id, msg_num, e);
                metrics.record_failure();
            }
        }
        
        // Throttle publishing
        if config.publish_interval_ms > 0 {
            sleep(Duration::from_millis(config.publish_interval_ms)).await;
        }
    }
    
    // Disconnect
    client.disconnect(None).await?;
    println!("Client {}: Completed", client_id);
    
    Ok(())
}

async fn run_load_test(config: LoadTestConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("Starting MQTT Load Test");
    println!("Clients: {}, Messages/Client: {}, Message Size: {} bytes",
             config.num_clients, config.messages_per_client, config.message_size);
    
    let metrics = Arc::new(TestMetrics::new());
    let mut handles: Vec<JoinHandle<_>> = Vec::new();
    
    let test_start = Instant::now();
    
    // Spawn all client tasks
    for client_id in 0..config.num_clients {
        let config_clone = config.clone();
        let metrics_clone = Arc::clone(&metrics);
        
        let handle = tokio::spawn(async move {
            if let Err(e) = mqtt_client_task(client_id, config_clone, metrics_clone).await {
                eprintln!("Client {} error: {}", client_id, e);
            }
        });
        
        handles.push(handle);
        
        // Stagger client creation to avoid connection storm
        sleep(Duration::from_millis(10)).await;
    }
    
    // Wait for all clients to complete
    for handle in handles {
        handle.await?;
    }
    
    let test_duration = test_start.elapsed();
    
    // Print results
    metrics.print_summary(test_duration, config.num_clients);
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = LoadTestConfig {
        num_clients: 100,
        messages_per_client: 100,
        message_size: 128,
        publish_interval_ms: 10,
        connection_timeout: Duration::from_secs(5),
    };
    
    run_load_test(config).await?;
    
    Ok(())
}
```

**Cargo.toml dependencies:**
```toml
[dependencies]
paho-mqtt = "0.12"
tokio = { version = "1", features = ["full"] }
```

## Advanced Load Testing Techniques

**Subscribe Load Testing**: Test the broker's ability to fan out messages to many subscribers by creating subscriber clients that measure message reception latency.

**Mixed Workloads**: Combine publishers and subscribers with different QoS levels to simulate real-world scenarios.

**Persistent Sessions**: Test with `clean_session=false` to evaluate session recovery under load.

**Large Message Testing**: Send larger payloads (4KB, 64KB) to test message handling and bandwidth limits.

**Will Messages**: Test last will and testament message delivery during connection failures.

## Tools for MQTT Load Testing

Several specialized tools exist for MQTT load testing:

**MQTT-Benchmark** (https://github.com/krylovsk/mqtt-benchmark): A simple Go-based tool for basic load testing with configurable client counts and message rates.

**emqtt_bench** (https://github.com/emqx/emqtt-bench): Enterprise-grade benchmarking tool from EMQ with support for massive concurrent connections.

**JMeter with MQTT Plugin**: Apache JMeter with MQTT support for complex test scenarios and detailed reporting.

**Locust with MQTT**: Python-based load testing with custom MQTT client scenarios.

## Best Practices

**Start Small**: Begin with a small number of clients and gradually increase to identify the scaling curve.

**Monitor Broker Resources**: Watch CPU, memory, network I/O, and file descriptors during tests.

**Test Different QoS Levels**: QoS 0, 1, and 2 have dramatically different performance characteristics.

**Use Realistic Message Patterns**: Match your test patterns to production usage (frequency, size, topics).

**Test Connection Recovery**: Simulate network failures and measure reconnection behavior.

**Isolate Test Environment**: Run load tests in a dedicated environment to avoid affecting production.

**Document Baseline Performance**: Record results to track performance changes over time.

**Test at Peak Plus Margin**: Test at 1.5-2x expected peak load to ensure headroom.

## Summary

Load testing MQTT systems is essential for building reliable IoT infrastructure. By simulating hundreds or thousands of concurrent clients, you can identify performance bottlenecks, validate scaling strategies, and ensure your broker can handle production workloads. The C/C++ example demonstrates multi-threaded load testing with detailed latency tracking, while the Rust example showcases async/await patterns for efficient concurrent client simulation.

Key takeaways include monitoring critical metrics like connection rate, message throughput, and latency; using realistic test scenarios that match production patterns; and testing at higher than expected loads to ensure system stability. Whether using custom scripts or specialized tools like emqtt_bench, regular load testing helps you build confidence in your MQTT infrastructure before deploying to production.