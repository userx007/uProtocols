# Load Testing WebSocket Connections

## Overview

Load testing WebSocket applications involves simulating thousands (or more) concurrent connections to measure performance characteristics like throughput, latency, memory consumption, and connection handling capacity. Unlike HTTP load testing, WebSocket testing requires maintaining persistent connections and simulating bidirectional communication patterns.

## Key Concepts

### What to Measure
- **Connection capacity**: Maximum concurrent connections the server can handle
- **Message throughput**: Messages per second in both directions
- **Latency**: Round-trip time for messages under various loads
- **Resource usage**: CPU, memory, network bandwidth consumption
- **Error rates**: Failed connections, dropped messages, timeouts
- **Degradation patterns**: How performance degrades as load increases

### Testing Patterns
- **Ramp-up testing**: Gradually increase connections to find breaking points
- **Sustained load**: Maintain constant load over extended periods
- **Spike testing**: Sudden traffic bursts to test elasticity
- **Stress testing**: Push beyond normal capacity to identify failure modes

## C Implementation

Here's a simple load testing client in C using libwebsockets:

```c
#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_CONNECTIONS 1000
#define MESSAGES_PER_CONNECTION 100

struct connection_data {
    int id;
    int messages_sent;
    int messages_received;
    struct timespec start_time;
    struct timespec first_message_time;
    double total_latency_ms;
};

struct test_stats {
    int connections_established;
    int connections_failed;
    int total_messages_sent;
    int total_messages_received;
    double avg_latency_ms;
    struct timespec test_start;
};

static struct test_stats stats = {0};
static volatile int interrupted = 0;
static struct lws_context *context;

static void sigint_handler(int sig) {
    interrupted = 1;
    lws_cancel_service(context);
}

static int callback_client(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
    struct connection_data *conn = (struct connection_data *)user;
    struct timespec now;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            clock_gettime(CLOCK_MONOTONIC, &conn->start_time);
            stats.connections_established++;
            printf("Connection %d established (total: %d)\n", 
                   conn->id, stats.connections_established);
            
            // Request callback to send first message
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            if (conn->messages_sent >= MESSAGES_PER_CONNECTION) {
                return 0;
            }
            
            unsigned char buf[LWS_PRE + 256];
            unsigned char *p = &buf[LWS_PRE];
            
            clock_gettime(CLOCK_MONOTONIC, &now);
            int n = snprintf((char *)p, 256, 
                           "Connection %d, Message %d, Time: %ld.%09ld",
                           conn->id, conn->messages_sent,
                           now.tv_sec, now.tv_nsec);
            
            int written = lws_write(wsi, p, n, LWS_WRITE_TEXT);
            if (written < n) {
                lwsl_err("Failed to write message\n");
                return -1;
            }
            
            conn->messages_sent++;
            stats.total_messages_sent++;
            
            if (conn->messages_sent < MESSAGES_PER_CONNECTION) {
                lws_callback_on_writable(wsi);
            }
            break;
        }
        
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            clock_gettime(CLOCK_MONOTONIC, &now);
            
            if (conn->messages_received == 0) {
                conn->first_message_time = now;
            }
            
            conn->messages_received++;
            stats.total_messages_received++;
            
            // Calculate latency (simple approximation)
            double latency = (now.tv_sec - conn->start_time.tv_sec) * 1000.0 +
                           (now.tv_nsec - conn->start_time.tv_nsec) / 1000000.0;
            conn->total_latency_ms += latency / conn->messages_received;
            
            if (conn->messages_received % 10 == 0) {
                printf("Connection %d: received %d/%d messages\n",
                       conn->id, conn->messages_received, MESSAGES_PER_CONNECTION);
            }
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            stats.connections_failed++;
            lwsl_err("Connection %d failed: %s\n", 
                    conn->id, in ? (char *)in : "unknown");
            break;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("Connection %d closed. Sent: %d, Received: %d\n",
                   conn->id, conn->messages_sent, conn->messages_received);
            break;
            
        default:
            break;
    }
    
    return 0;
}

static const struct lws_protocols protocols[] = {
    {
        "load-test-protocol",
        callback_client,
        sizeof(struct connection_data),
        4096,
    },
    { NULL, NULL, 0, 0 }
};

void print_statistics() {
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double duration = (end.tv_sec - stats.test_start.tv_sec) +
                     (end.tv_nsec - stats.test_start.tv_nsec) / 1e9;
    
    printf("\n=== Load Test Statistics ===\n");
    printf("Test Duration: %.2f seconds\n", duration);
    printf("Connections Established: %d\n", stats.connections_established);
    printf("Connections Failed: %d\n", stats.connections_failed);
    printf("Total Messages Sent: %d\n", stats.total_messages_sent);
    printf("Total Messages Received: %d\n", stats.total_messages_received);
    printf("Messages/sec (sent): %.2f\n", stats.total_messages_sent / duration);
    printf("Messages/sec (received): %.2f\n", stats.total_messages_received / duration);
    printf("===========================\n");
}

int main(int argc, char **argv) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    int num_connections = 100;
    
    if (argc > 1) {
        num_connections = atoi(argv[1]);
        if (num_connections > MAX_CONNECTIONS) {
            num_connections = MAX_CONNECTIONS;
        }
    }
    
    signal(SIGINT, sigint_handler);
    clock_gettime(CLOCK_MONOTONIC, &stats.test_start);
    
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create context\n");
        return 1;
    }
    
    printf("Starting load test with %d connections...\n", num_connections);
    
    // Create connections with slight delay to avoid overwhelming the server
    for (int i = 0; i < num_connections && !interrupted; i++) {
        memset(&connect_info, 0, sizeof(connect_info));
        connect_info.context = context;
        connect_info.address = "localhost";
        connect_info.port = 9001;
        connect_info.path = "/";
        connect_info.host = connect_info.address;
        connect_info.origin = connect_info.address;
        connect_info.protocol = protocols[0].name;
        
        struct lws *wsi = lws_client_connect_via_info(&connect_info);
        if (!wsi) {
            lwsl_err("Failed to create connection %d\n", i);
            stats.connections_failed++;
        }
        
        // Small delay between connections
        if (i % 10 == 0 && i > 0) {
            lws_service(context, 10);
        }
    }
    
    // Service loop
    while (!interrupted && 
           (stats.total_messages_received < num_connections * MESSAGES_PER_CONNECTION)) {
        lws_service(context, 50);
    }
    
    print_statistics();
    
    lws_context_destroy(context);
    return 0;
}
```

## C++ Implementation

A more modern C++ approach using Boost.Beast for load testing:

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <iostream>
#include <memory>
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class LoadTestStats {
public:
    std::atomic<int> connections_established{0};
    std::atomic<int> connections_failed{0};
    std::atomic<int> messages_sent{0};
    std::atomic<int> messages_received{0};
    std::atomic<long long> total_latency_us{0};
    std::chrono::steady_clock::time_point start_time;
    
    void print_stats() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            end - start_time).count();
        
        std::cout << "\n=== Load Test Statistics ===\n";
        std::cout << "Duration: " << duration << " seconds\n";
        std::cout << "Connections Established: " << connections_established << "\n";
        std::cout << "Connections Failed: " << connections_failed << "\n";
        std::cout << "Messages Sent: " << messages_sent << "\n";
        std::cout << "Messages Received: " << messages_received << "\n";
        
        if (duration > 0) {
            std::cout << "Messages/sec (sent): " 
                     << messages_sent.load() / duration << "\n";
            std::cout << "Messages/sec (received): " 
                     << messages_received.load() / duration << "\n";
        }
        
        if (messages_received > 0) {
            std::cout << "Average Latency: " 
                     << total_latency_us.load() / messages_received.load()
                     << " microseconds\n";
        }
        std::cout << "===========================\n";
    }
};

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::string host_;
    int id_;
    int messages_to_send_;
    int messages_sent_{0};
    std::shared_ptr<LoadTestStats> stats_;
    std::chrono::steady_clock::time_point send_time_;

public:
    explicit WebSocketSession(net::io_context& ioc, 
                             const std::string& host,
                             int id,
                             int messages_to_send,
                             std::shared_ptr<LoadTestStats> stats)
        : ws_(net::make_strand(ioc))
        , host_(host)
        , id_(id)
        , messages_to_send_(messages_to_send)
        , stats_(stats)
    {
    }

    void run(const std::string& host, const std::string& port, 
             const std::string& path) {
        tcp::resolver resolver(net::make_strand(ws_.get_executor()));
        
        auto const results = resolver.resolve(host, port);
        
        beast::get_lowest_layer(ws_).connect(results);
        
        ws_.handshake(host_, path);
        stats_->connections_established++;
        
        std::cout << "Connection " << id_ << " established\n";
        
        write_message();
    }

private:
    void write_message() {
        if (messages_sent_ >= messages_to_send_) {
            ws_.close(websocket::close_code::normal);
            return;
        }
        
        std::string message = "Connection " + std::to_string(id_) + 
                             ", Message " + std::to_string(messages_sent_);
        
        send_time_ = std::chrono::steady_clock::now();
        ws_.write(net::buffer(message));
        
        messages_sent_++;
        stats_->messages_sent++;
        
        read_message();
    }
    
    void read_message() {
        buffer_.clear();
        ws_.read(buffer_);
        
        auto receive_time = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            receive_time - send_time_).count();
        
        stats_->total_latency_us += latency;
        stats_->messages_received++;
        
        if (messages_sent_ % 10 == 0) {
            std::cout << "Connection " << id_ << ": " 
                     << messages_sent_ << "/" << messages_to_send_ 
                     << " messages (latency: " << latency << "us)\n";
        }
        
        write_message();
    }
};

int main(int argc, char* argv[]) {
    try {
        int num_connections = 100;
        int messages_per_connection = 100;
        
        if (argc > 1) num_connections = std::atoi(argv[1]);
        if (argc > 2) messages_per_connection = std::atoi(argv[2]);
        
        auto stats = std::make_shared<LoadTestStats>();
        stats->start_time = std::chrono::steady_clock::now();
        
        std::cout << "Starting load test with " << num_connections 
                 << " connections, " << messages_per_connection 
                 << " messages each...\n";
        
        // Create thread pool for concurrent connections
        net::thread_pool pool(std::thread::hardware_concurrency());
        
        std::vector<std::future<void>> futures;
        
        for (int i = 0; i < num_connections; ++i) {
            futures.push_back(
                net::post(pool, std::packaged_task<void()>([i, messages_per_connection, stats]() {
                    try {
                        net::io_context ioc;
                        auto session = std::make_shared<WebSocketSession>(
                            ioc, "localhost", i, messages_per_connection, stats);
                        
                        session->run("localhost", "9001", "/");
                    } catch (std::exception const& e) {
                        stats->connections_failed++;
                        std::cerr << "Connection " << i << " error: " 
                                 << e.what() << "\n";
                    }
                }))
            );
            
            // Throttle connection creation
            if (i % 10 == 0 && i > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Wait for all connections to complete
        for (auto& f : futures) {
            f.wait();
        }
        
        pool.join();
        stats->print_stats();
        
    } catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
```

## Rust Implementation

Here's a comprehensive load testing implementation in Rust using `tokio-tungstenite`:

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tokio::time::{Duration, Instant};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};

#[derive(Debug)]
struct LoadTestStats {
    connections_established: AtomicUsize,
    connections_failed: AtomicUsize,
    messages_sent: AtomicUsize,
    messages_received: AtomicUsize,
    total_latency_us: AtomicU64,
    start_time: Instant,
}

impl LoadTestStats {
    fn new() -> Self {
        Self {
            connections_established: AtomicUsize::new(0),
            connections_failed: AtomicUsize::new(0),
            messages_sent: AtomicUsize::new(0),
            messages_received: AtomicUsize::new(0),
            total_latency_us: AtomicU64::new(0),
            start_time: Instant::now(),
        }
    }
    
    fn print_stats(&self) {
        let duration = self.start_time.elapsed().as_secs();
        let established = self.connections_established.load(Ordering::Relaxed);
        let failed = self.connections_failed.load(Ordering::Relaxed);
        let sent = self.messages_sent.load(Ordering::Relaxed);
        let received = self.messages_received.load(Ordering::Relaxed);
        let total_latency = self.total_latency_us.load(Ordering::Relaxed);
        
        println!("\n=== Load Test Statistics ===");
        println!("Duration: {} seconds", duration);
        println!("Connections Established: {}", established);
        println!("Connections Failed: {}", failed);
        println!("Messages Sent: {}", sent);
        println!("Messages Received: {}", received);
        
        if duration > 0 {
            println!("Messages/sec (sent): {}", sent / duration as usize);
            println!("Messages/sec (received): {}", received / duration as usize);
        }
        
        if received > 0 {
            println!("Average Latency: {} microseconds", 
                    total_latency / received as u64);
        }
        println!("===========================");
    }
}

async fn run_connection(
    id: usize,
    url: &str,
    messages_to_send: usize,
    stats: Arc<LoadTestStats>,
) -> Result<(), Box<dyn std::error::Error>> {
    // Connect to WebSocket
    let (ws_stream, _) = match connect_async(url).await {
        Ok(result) => {
            stats.connections_established.fetch_add(1, Ordering::Relaxed);
            println!("Connection {} established", id);
            result
        }
        Err(e) => {
            stats.connections_failed.fetch_add(1, Ordering::Relaxed);
            return Err(Box::new(e));
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn read task
    let stats_clone = Arc::clone(&stats);
    let read_handle = tokio::spawn(async move {
        let mut count = 0;
        while let Some(message) = read.next().await {
            match message {
                Ok(Message::Text(_)) | Ok(Message::Binary(_)) => {
                    stats_clone.messages_received.fetch_add(1, Ordering::Relaxed);
                    count += 1;
                    
                    if count % 10 == 0 {
                        println!("Connection {}: received {}/{} messages", 
                                id, count, messages_to_send);
                    }
                }
                Ok(Message::Close(_)) => break,
                Err(e) => {
                    eprintln!("Connection {} read error: {}", id, e);
                    break;
                }
                _ => {}
            }
        }
    });
    
    // Send messages
    for i in 0..messages_to_send {
        let send_time = Instant::now();
        let message = format!("Connection {}, Message {}", id, i);
        
        if let Err(e) = write.send(Message::Text(message)).await {
            eprintln!("Connection {} send error: {}", id, e);
            break;
        }
        
        let latency = send_time.elapsed().as_micros() as u64;
        stats.total_latency_us.fetch_add(latency, Ordering::Relaxed);
        stats.messages_sent.fetch_add(1, Ordering::Relaxed);
        
        // Small delay between messages
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    
    // Close connection
    write.send(Message::Close(None)).await?;
    
    // Wait for read task to finish
    let _ = read_handle.await;
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    let num_connections = args.get(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(100);
    let messages_per_connection = args.get(2)
        .and_then(|s| s.parse().ok())
        .unwrap_or(100);
    
    let url = "ws://localhost:9001";
    let stats = Arc::new(LoadTestStats::new());
    
    println!("Starting load test with {} connections, {} messages each...", 
             num_connections, messages_per_connection);
    
    let mut handles = Vec::new();
    
    // Create connections with throttling
    for i in 0..num_connections {
        let url = url.to_string();
        let stats = Arc::clone(&stats);
        
        let handle = tokio::spawn(async move {
            if let Err(e) = run_connection(i, &url, messages_per_connection, stats).await {
                eprintln!("Connection {} failed: {}", i, e);
            }
        });
        
        handles.push(handle);
        
        // Throttle connection creation
        if i % 10 == 0 && i > 0 {
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    }
    
    // Wait for all connections to complete
    for handle in handles {
        let _ = handle.await;
    }
    
    stats.print_stats();
    
    Ok(())
}
```

### Advanced Rust Load Testing with Metrics

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tokio::time::{Duration, Instant};
use std::sync::Arc;
use parking_lot::Mutex;
use hdrhistogram::Histogram;

#[derive(Clone)]
struct MetricsCollector {
    latency_histogram: Arc<Mutex<Histogram<u64>>>,
    connection_times: Arc<Mutex<Vec<Duration>>>,
    error_counts: Arc<Mutex<std::collections::HashMap<String, usize>>>,
}

impl MetricsCollector {
    fn new() -> Self {
        Self {
            latency_histogram: Arc::new(Mutex::new(
                Histogram::<u64>::new_with_max(60_000_000, 3).unwrap()
            )),
            connection_times: Arc::new(Mutex::new(Vec::new())),
            error_counts: Arc::new(Mutex::new(std::collections::HashMap::new())),
        }
    }
    
    fn record_latency(&self, latency_us: u64) {
        let mut hist = self.latency_histogram.lock();
        let _ = hist.record(latency_us);
    }
    
    fn record_connection_time(&self, duration: Duration) {
        self.connection_times.lock().push(duration);
    }
    
    fn record_error(&self, error_type: String) {
        let mut errors = self.error_counts.lock();
        *errors.entry(error_type).or_insert(0) += 1;
    }
    
    fn print_report(&self) {
        let hist = self.latency_histogram.lock();
        let conn_times = self.connection_times.lock();
        let errors = self.error_counts.lock();
        
        println!("\n=== Detailed Metrics Report ===");
        
        // Latency percentiles
        println!("\nLatency Distribution (microseconds):");
        println!("  Min: {}", hist.min());
        println!("  P50: {}", hist.value_at_quantile(0.50));
        println!("  P90: {}", hist.value_at_quantile(0.90));
        println!("  P95: {}", hist.value_at_quantile(0.95));
        println!("  P99: {}", hist.value_at_quantile(0.99));
        println!("  Max: {}", hist.max());
        
        // Connection times
        if !conn_times.is_empty() {
            let avg_conn_time: Duration = conn_times.iter().sum::<Duration>() 
                / conn_times.len() as u32;
            println!("\nAverage Connection Time: {:?}", avg_conn_time);
        }
        
        // Error breakdown
        if !errors.is_empty() {
            println!("\nError Breakdown:");
            for (error_type, count) in errors.iter() {
                println!("  {}: {}", error_type, count);
            }
        }
        
        println!("==============================");
    }
}

async fn benchmark_connection(
    id: usize,
    url: &str,
    messages_to_send: usize,
    metrics: MetricsCollector,
) -> Result<(), Box<dyn std::error::Error>> {
    let connect_start = Instant::now();
    
    let (ws_stream, _) = match connect_async(url).await {
        Ok(result) => {
            metrics.record_connection_time(connect_start.elapsed());
            result
        }
        Err(e) => {
            metrics.record_error("connection_failed".to_string());
            return Err(Box::new(e));
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    for i in 0..messages_to_send {
        let send_time = Instant::now();
        let message = format!("Benchmark message {}", i);
        
        write.send(Message::Text(message)).await?;
        
        // Wait for echo
        if let Some(Ok(_)) = read.next().await {
            let latency = send_time.elapsed().as_micros() as u64;
            metrics.record_latency(latency);
        }
    }
    
    write.send(Message::Close(None)).await?;
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let num_connections = 1000;
    let messages_per_connection = 100;
    let url = "ws://localhost:9001";
    
    let metrics = MetricsCollector::new();
    let mut handles = Vec::new();
    
    println!("Starting benchmark with {} connections...", num_connections);
    let test_start = Instant::now();
    
    for i in 0..num_connections {
        let url = url.to_string();
        let metrics = metrics.clone();
        
        let handle = tokio::spawn(async move {
            let _ = benchmark_connection(i, &url, messages_per_connection, metrics).await;
        });
        
        handles.push(handle);
    }
    
    for handle in handles {
        let _ = handle.await;
    }
    
    let test_duration = test_start.elapsed();
    println!("\nTest completed in {:?}", test_duration);
    
    metrics.print_report();
    
    Ok(())
}
```

## Summary

**Load testing WebSocket applications** is crucial for understanding system capacity and performance characteristics. Key considerations include:

- **Concurrent connections**: Simulating realistic user counts with thousands of persistent connections
- **Bidirectional traffic**: Testing both client-to-server and server-to-client message flows
- **Metrics collection**: Tracking latency percentiles, throughput, error rates, and resource usage
- **Progressive loading**: Ramping up connections gradually to identify breaking points
- **Real-world patterns**: Simulating actual usage patterns (message rates, payload sizes, connection lifetimes)

The examples demonstrate:
- **C**: Using libwebsockets for basic load testing with connection tracking
- **C++**: Boost.Beast with thread pools for concurrent connection management
- **Rust**: Tokio-based async implementation with advanced metrics collection using histograms

Effective load testing helps identify bottlenecks, validate scalability assumptions, and ensure your WebSocket infrastructure can handle production traffic before deployment.