# 54. Automatic Reconnection in WebSocket Programming

## Overview

Automatic reconnection is a critical feature in WebSocket applications that ensures resilience against network failures, server restarts, and temporary connectivity issues. Without proper reconnection logic, a single network hiccup could permanently disconnect users, resulting in poor user experience and data loss.

## Core Concepts

### Why Automatic Reconnection Matters

1. **Network Instability**: Mobile networks, Wi-Fi transitions, and ISP issues cause temporary disconnections
2. **Server Maintenance**: Planned/unplanned server restarts require client reconnection
3. **Load Balancing**: Connection drops during server scaling or load balancing operations
4. **User Experience**: Seamless reconnection prevents data loss and maintains application state

### Key Components

1. **Exponential Backoff**: Progressively increasing delay between reconnection attempts
2. **Jitter**: Random variation in retry delays to prevent thundering herd problem
3. **Maximum Retry Limits**: Cap on total attempts or maximum delay time
4. **Connection State Management**: Tracking connection status and attempt history
5. **Message Queue**: Buffering messages during disconnection for replay

## Exponential Backoff Strategy

The exponential backoff algorithm:
```
delay = min(initial_delay * (multiplier ^ attempt), max_delay) + jitter
```

**Parameters**:
- `initial_delay`: Starting delay (e.g., 1 second)
- `multiplier`: Growth factor (typically 2)
- `attempt`: Current retry attempt number
- `max_delay`: Maximum delay cap (e.g., 60 seconds)
- `jitter`: Random value to prevent synchronized retries

## C/C++ Implementation

### Using libwebsockets

```cpp
#include <libwebsockets.h>
#include <string>
#include <chrono>
#include <random>
#include <queue>
#include <memory>
#include <iostream>

class WebSocketReconnector {
private:
    struct lws_context* context;
    struct lws* wsi;
    
    // Reconnection parameters
    int reconnect_attempt;
    int max_reconnect_attempts;
    int initial_delay_ms;
    int max_delay_ms;
    double backoff_multiplier;
    
    // Connection state
    bool should_reconnect;
    bool is_connected;
    std::chrono::steady_clock::time_point next_reconnect_time;
    
    // Message queue for pending messages
    std::queue<std::string> message_queue;
    
    // Random generator for jitter
    std::mt19937 rng;
    std::uniform_real_distribution<double> jitter_dist;
    
    std::string uri;
    std::string protocol;

public:
    WebSocketReconnector(const std::string& uri, const std::string& protocol = "")
        : context(nullptr), wsi(nullptr), reconnect_attempt(0),
          max_reconnect_attempts(10), initial_delay_ms(1000),
          max_delay_ms(60000), backoff_multiplier(2.0),
          should_reconnect(true), is_connected(false),
          rng(std::random_device{}()), jitter_dist(0.0, 1.0),
          uri(uri), protocol(protocol) {}
    
    ~WebSocketReconnector() {
        cleanup();
    }
    
    // Calculate next reconnection delay with exponential backoff and jitter
    int calculateBackoffDelay() {
        // Exponential backoff: initial_delay * (multiplier ^ attempt)
        double delay = initial_delay_ms * std::pow(backoff_multiplier, reconnect_attempt);
        
        // Cap at maximum delay
        delay = std::min(delay, static_cast<double>(max_delay_ms));
        
        // Add jitter (±25% of delay)
        double jitter = delay * 0.25 * (jitter_dist(rng) * 2.0 - 1.0);
        
        return static_cast<int>(delay + jitter);
    }
    
    // Initialize WebSocket connection
    bool connect() {
        struct lws_context_creation_info info;
        memset(&info, 0, sizeof(info));
        
        info.port = CONTEXT_PORT_NO_LISTEN;
        info.protocols = protocols;
        info.gid = -1;
        info.uid = -1;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.user = this;
        
        context = lws_create_context(&info);
        if (!context) {
            std::cerr << "Failed to create context" << std::endl;
            return false;
        }
        
        return attemptConnection();
    }
    
    // Attempt to establish WebSocket connection
    bool attemptConnection() {
        struct lws_client_connect_info ccinfo;
        memset(&ccinfo, 0, sizeof(ccinfo));
        
        ccinfo.context = context;
        ccinfo.address = "example.com";  // Parse from uri
        ccinfo.port = 443;
        ccinfo.path = "/websocket";
        ccinfo.host = ccinfo.address;
        ccinfo.origin = ccinfo.address;
        ccinfo.protocol = protocol.empty() ? nullptr : protocol.c_str();
        ccinfo.ssl_connection = LCCSCF_USE_SSL;
        ccinfo.userdata = this;
        
        wsi = lws_client_connect_via_info(&ccinfo);
        
        return wsi != nullptr;
    }
    
    // Handle connection failure and schedule reconnection
    void onConnectionFailed() {
        is_connected = false;
        
        if (!should_reconnect || reconnect_attempt >= max_reconnect_attempts) {
            std::cerr << "Max reconnection attempts reached" << std::endl;
            return;
        }
        
        int delay_ms = calculateBackoffDelay();
        next_reconnect_time = std::chrono::steady_clock::now() + 
                              std::chrono::milliseconds(delay_ms);
        
        std::cout << "Reconnecting in " << delay_ms << "ms (attempt " 
                  << (reconnect_attempt + 1) << ")" << std::endl;
        
        reconnect_attempt++;
    }
    
    // Handle successful connection
    void onConnected() {
        is_connected = true;
        reconnect_attempt = 0;  // Reset counter on successful connection
        
        std::cout << "WebSocket connected successfully" << std::endl;
        
        // Flush queued messages
        flushMessageQueue();
    }
    
    // Handle disconnection
    void onDisconnected() {
        is_connected = false;
        wsi = nullptr;
        
        std::cout << "WebSocket disconnected" << std::endl;
        
        if (should_reconnect) {
            onConnectionFailed();
        }
    }
    
    // Send message (queue if not connected)
    bool sendMessage(const std::string& message) {
        if (!is_connected) {
            message_queue.push(message);
            return false;
        }
        
        size_t len = message.length();
        unsigned char* buf = new unsigned char[LWS_PRE + len];
        memcpy(&buf[LWS_PRE], message.c_str(), len);
        
        int n = lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
        delete[] buf;
        
        return n >= 0;
    }
    
    // Flush queued messages after reconnection
    void flushMessageQueue() {
        while (!message_queue.empty() && is_connected) {
            const std::string& msg = message_queue.front();
            if (sendMessage(msg)) {
                message_queue.pop();
            } else {
                break;
            }
        }
    }
    
    // Main service loop
    void service() {
        // Check if we need to reconnect
        if (!is_connected && should_reconnect && wsi == nullptr) {
            auto now = std::chrono::steady_clock::now();
            if (now >= next_reconnect_time) {
                attemptConnection();
            }
        }
        
        if (context) {
            lws_service(context, 50);
        }
    }
    
    void cleanup() {
        should_reconnect = false;
        if (context) {
            lws_context_destroy(context);
            context = nullptr;
        }
    }
    
    // libwebsockets protocol callbacks
    static struct lws_protocols protocols[];
    
    static int callback(struct lws* wsi, enum lws_callback_reasons reason,
                       void* user, void* in, size_t len) {
        auto* reconnector = static_cast<WebSocketReconnector*>(
            lws_context_user(lws_get_context(wsi)));
        
        switch (reason) {
            case LWS_CALLBACK_CLIENT_ESTABLISHED:
                reconnector->onConnected();
                break;
                
            case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
                reconnector->onConnectionFailed();
                break;
                
            case LWS_CALLBACK_CLIENT_CLOSED:
                reconnector->onDisconnected();
                break;
                
            case LWS_CALLBACK_CLIENT_RECEIVE:
                // Handle received data
                std::cout << "Received: " << std::string((char*)in, len) << std::endl;
                break;
                
            case LWS_CALLBACK_CLIENT_WRITEABLE:
                // Handle writable state
                break;
                
            default:
                break;
        }
        
        return 0;
    }
};

struct lws_protocols WebSocketReconnector::protocols[] = {
    {
        "ws-protocol",
        WebSocketReconnector::callback,
        0,
        4096,
    },
    { NULL, NULL, 0, 0 }
};

// Usage example
int main() {
    WebSocketReconnector reconnector("wss://example.com/websocket", "ws-protocol");
    
    if (!reconnector.connect()) {
        std::cerr << "Initial connection failed" << std::endl;
        return 1;
    }
    
    // Main loop
    while (true) {
        reconnector.service();
        
        // Send messages
        reconnector.sendMessage("Hello, WebSocket!");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

## Rust Implementation

### Using tokio-tungstenite

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tokio::time::{sleep, Duration, Instant};
use url::Url;
use std::sync::Arc;
use tokio::sync::{mpsc, Mutex};
use rand::Rng;

#[derive(Clone)]
struct ReconnectionConfig {
    initial_delay_ms: u64,
    max_delay_ms: u64,
    backoff_multiplier: f64,
    max_attempts: Option<u32>,
    jitter_factor: f64,
}

impl Default for ReconnectionConfig {
    fn default() -> Self {
        Self {
            initial_delay_ms: 1000,
            max_delay_ms: 60000,
            backoff_multiplier: 2.0,
            max_attempts: Some(10),
            jitter_factor: 0.25,
        }
    }
}

struct WebSocketReconnector {
    url: Url,
    config: ReconnectionConfig,
    reconnect_attempt: Arc<Mutex<u32>>,
    message_queue: Arc<Mutex<Vec<String>>>,
    tx: mpsc::UnboundedSender<Message>,
    rx: Arc<Mutex<mpsc::UnboundedReceiver<Message>>>,
}

impl WebSocketReconnector {
    pub fn new(url: &str, config: ReconnectionConfig) -> Result<Self, url::ParseError> {
        let (tx, rx) = mpsc::unbounded_channel();
        
        Ok(Self {
            url: Url::parse(url)?,
            config,
            reconnect_attempt: Arc::new(Mutex::new(0)),
            message_queue: Arc::new(Mutex::new(Vec::new())),
            tx,
            rx: Arc::new(Mutex::new(rx)),
        })
    }
    
    // Calculate exponential backoff delay with jitter
    fn calculate_backoff_delay(&self, attempt: u32) -> Duration {
        let mut rng = rand::thread_rng();
        
        // Exponential backoff
        let delay_ms = (self.config.initial_delay_ms as f64) 
            * self.config.backoff_multiplier.powi(attempt as i32);
        
        // Cap at maximum
        let delay_ms = delay_ms.min(self.config.max_delay_ms as f64);
        
        // Add jitter (±jitter_factor of delay)
        let jitter_range = delay_ms * self.config.jitter_factor;
        let jitter = rng.gen_range(-jitter_range..=jitter_range);
        
        let total_delay = (delay_ms + jitter).max(0.0) as u64;
        
        Duration::from_millis(total_delay)
    }
    
    // Attempt to establish WebSocket connection
    async fn attempt_connection(&self) -> Result<(), Box<dyn std::error::Error>> {
        let mut attempt = self.reconnect_attempt.lock().await;
        
        // Check if max attempts reached
        if let Some(max) = self.config.max_attempts {
            if *attempt >= max {
                return Err("Max reconnection attempts reached".into());
            }
        }
        
        // Calculate delay for this attempt
        if *attempt > 0 {
            let delay = self.calculate_backoff_delay(*attempt);
            println!("Reconnecting in {:?} (attempt {})", delay, *attempt + 1);
            sleep(delay).await;
        }
        
        *attempt += 1;
        
        Ok(())
    }
    
    // Main connection loop with automatic reconnection
    pub async fn connect_with_retry(self: Arc<Self>) -> Result<(), Box<dyn std::error::Error>> {
        loop {
            // Attempt connection with backoff
            if let Err(e) = self.attempt_connection().await {
                eprintln!("Reconnection failed: {}", e);
                return Err(e);
            }
            
            // Try to establish WebSocket connection
            match connect_async(&self.url).await {
                Ok((ws_stream, _)) => {
                    println!("WebSocket connected successfully");
                    
                    // Reset attempt counter on successful connection
                    *self.reconnect_attempt.lock().await = 0;
                    
                    // Split stream
                    let (mut write, mut read) = ws_stream.split();
                    
                    // Flush queued messages
                    let queued_messages = {
                        let mut queue = self.message_queue.lock().await;
                        queue.drain(..).collect::<Vec<_>>()
                    };
                    
                    for msg in queued_messages {
                        if let Err(e) = write.send(Message::Text(msg)).await {
                            eprintln!("Failed to send queued message: {}", e);
                        }
                    }
                    
                    // Handle connection
                    let self_clone = Arc::clone(&self);
                    let write_handle = tokio::spawn(async move {
                        let mut rx = self_clone.rx.lock().await;
                        while let Some(msg) = rx.recv().await {
                            if write.send(msg).await.is_err() {
                                break;
                            }
                        }
                    });
                    
                    // Read messages
                    let disconnect_reason = loop {
                        match read.next().await {
                            Some(Ok(msg)) => {
                                match msg {
                                    Message::Text(text) => {
                                        println!("Received: {}", text);
                                    }
                                    Message::Binary(data) => {
                                        println!("Received binary: {} bytes", data.len());
                                    }
                                    Message::Ping(data) => {
                                        println!("Received ping");
                                        // Pong is handled automatically
                                    }
                                    Message::Close(frame) => {
                                        println!("Connection closed: {:?}", frame);
                                        break "Server closed connection";
                                    }
                                    _ => {}
                                }
                            }
                            Some(Err(e)) => {
                                eprintln!("Error reading message: {}", e);
                                break "Read error";
                            }
                            None => {
                                break "Stream ended";
                            }
                        }
                    };
                    
                    // Clean up
                    write_handle.abort();
                    
                    println!("Disconnected: {}. Attempting reconnection...", disconnect_reason);
                }
                Err(e) => {
                    eprintln!("Connection failed: {}", e);
                    // Loop will retry with backoff
                }
            }
        }
    }
    
    // Send message (queue if not connected)
    pub async fn send_message(&self, message: String) -> Result<(), Box<dyn std::error::Error>> {
        // Try to send immediately
        if self.tx.send(Message::Text(message.clone())).is_err() {
            // If send fails, queue the message
            let mut queue = self.message_queue.lock().await;
            queue.push(message);
        }
        
        Ok(())
    }
}

// Usage example
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = ReconnectionConfig {
        initial_delay_ms: 1000,
        max_delay_ms: 60000,
        backoff_multiplier: 2.0,
        max_attempts: None, // Infinite retries
        jitter_factor: 0.25,
    };
    
    let reconnector = Arc::new(
        WebSocketReconnector::new("wss://echo.websocket.org/", config)?
    );
    
    // Spawn connection handler
    let reconnector_clone = Arc::clone(&reconnector);
    tokio::spawn(async move {
        if let Err(e) = reconnector_clone.connect_with_retry().await {
            eprintln!("Fatal error: {}", e);
        }
    });
    
    // Send messages periodically
    let mut interval = tokio::time::interval(Duration::from_secs(2));
    let mut counter = 0;
    
    loop {
        interval.tick().await;
        
        let message = format!("Message #{}", counter);
        if let Err(e) = reconnector.send_message(message).await {
            eprintln!("Failed to send message: {}", e);
        }
        
        counter += 1;
    }
}
```

### Advanced Rust Implementation with Connection State

```rust
use tokio::sync::RwLock;
use std::sync::atomic::{AtomicBool, Ordering};

#[derive(Debug, Clone, Copy, PartialEq)]
enum ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Failed,
}

struct AdvancedWebSocketClient {
    url: Url,
    config: ReconnectionConfig,
    state: Arc<RwLock<ConnectionState>>,
    should_reconnect: Arc<AtomicBool>,
    reconnect_attempt: Arc<Mutex<u32>>,
    message_queue: Arc<Mutex<Vec<Message>>>,
    // Event callbacks
    on_connected: Option<Box<dyn Fn() + Send + Sync>>,
    on_disconnected: Option<Box<dyn Fn() + Send + Sync>>,
    on_message: Option<Box<dyn Fn(Message) + Send + Sync>>,
}

impl AdvancedWebSocketClient {
    pub fn new(url: &str, config: ReconnectionConfig) -> Result<Self, url::ParseError> {
        Ok(Self {
            url: Url::parse(url)?,
            config,
            state: Arc::new(RwLock::new(ConnectionState::Disconnected)),
            should_reconnect: Arc::new(AtomicBool::new(true)),
            reconnect_attempt: Arc::new(Mutex::new(0)),
            message_queue: Arc::new(Mutex::new(Vec::new())),
            on_connected: None,
            on_disconnected: None,
            on_message: None,
        })
    }
    
    pub fn on_connected<F>(mut self, callback: F) -> Self 
    where 
        F: Fn() + Send + Sync + 'static 
    {
        self.on_connected = Some(Box::new(callback));
        self
    }
    
    pub async fn get_state(&self) -> ConnectionState {
        *self.state.read().await
    }
    
    pub fn stop_reconnecting(&self) {
        self.should_reconnect.store(false, Ordering::Relaxed);
    }
    
    // Enhanced connection logic with state management
    pub async fn run(self: Arc<Self>) {
        while self.should_reconnect.load(Ordering::Relaxed) {
            // Set state to connecting/reconnecting
            {
                let mut state = self.state.write().await;
                *state = if *self.reconnect_attempt.lock().await == 0 {
                    ConnectionState::Connecting
                } else {
                    ConnectionState::Reconnecting
                };
            }
            
            // Calculate and apply backoff
            let attempt = *self.reconnect_attempt.lock().await;
            if attempt > 0 {
                let delay = self.calculate_backoff_delay(attempt);
                println!("Waiting {:?} before reconnection attempt {}", delay, attempt + 1);
                sleep(delay).await;
            }
            
            // Attempt connection
            match connect_async(&self.url).await {
                Ok((ws_stream, _)) => {
                    println!("✓ Connected to {}", self.url);
                    
                    // Update state
                    *self.state.write().await = ConnectionState::Connected;
                    *self.reconnect_attempt.lock().await = 0;
                    
                    // Trigger callback
                    if let Some(ref callback) = self.on_connected {
                        callback();
                    }
                    
                    // Handle connection (this will block until disconnection)
                    self.handle_connection(ws_stream).await;
                    
                    // Update state after disconnection
                    *self.state.write().await = ConnectionState::Disconnected;
                    
                    // Trigger callback
                    if let Some(ref callback) = self.on_disconnected {
                        callback();
                    }
                }
                Err(e) => {
                    eprintln!("✗ Connection failed: {}", e);
                    *self.reconnect_attempt.lock().await += 1;
                    
                    // Check if max attempts reached
                    if let Some(max) = self.config.max_attempts {
                        if *self.reconnect_attempt.lock().await >= max {
                            *self.state.write().await = ConnectionState::Failed;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    fn calculate_backoff_delay(&self, attempt: u32) -> Duration {
        let mut rng = rand::thread_rng();
        let base_delay = (self.config.initial_delay_ms as f64)
            * self.config.backoff_multiplier.powi(attempt as i32);
        let capped_delay = base_delay.min(self.config.max_delay_ms as f64);
        let jitter = rng.gen_range(-capped_delay * 0.25..=capped_delay * 0.25);
        Duration::from_millis((capped_delay + jitter).max(0.0) as u64)
    }
    
    async fn handle_connection(
        &self,
        ws_stream: tokio_tungstenite::WebSocketStream
            tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>
        >
    ) {
        // Implementation details...
    }
}
```

## Summary

**Automatic reconnection** is essential for production WebSocket applications, providing resilience against network failures and server issues. Key takeaways:

### Core Principles
1. **Exponential Backoff**: Progressively increasing delays prevent server overload and reduce network congestion
2. **Jitter**: Random delay variations prevent synchronized reconnection storms (thundering herd)
3. **State Management**: Tracking connection states enables proper handling of queued messages and user feedback
4. **Message Queuing**: Buffering messages during disconnection ensures no data loss

### Best Practices
- Start with 1-second initial delay, cap at 60 seconds
- Use 2x backoff multiplier with ±25% jitter
- Implement maximum retry limits to prevent infinite loops
- Reset counters on successful connection
- Provide user feedback during reconnection attempts
- Queue outgoing messages during disconnection
- Handle graceful shutdowns to prevent unnecessary retries

### Implementation Considerations
- **C/C++**: Lower-level control with libwebsockets, manual memory management
- **Rust**: Type-safe async/await patterns with tokio, automatic resource cleanup
- Both implementations demonstrate production-ready patterns with error handling, state management, and configurable retry logic

Proper reconnection strategies transform fragile WebSocket connections into robust, production-ready communication channels that gracefully handle real-world network conditions.