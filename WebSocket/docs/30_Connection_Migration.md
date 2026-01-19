# WebSocket Connection Migration

Connection migration in WebSockets refers to the ability to handle network changes, connection failures, and seamless reconnection strategies without disrupting the user experience. This is crucial for maintaining persistent real-time communication in mobile environments, unreliable networks, or when switching between network interfaces (WiFi to cellular, for example).

## Core Concepts

**Connection Migration** involves several key aspects:

1. **Failover Detection**: Identifying when a connection has been lost or degraded
2. **State Preservation**: Maintaining application state during reconnection
3. **Seamless Reconnection**: Re-establishing the connection without data loss
4. **Session Continuity**: Ensuring the server recognizes the reconnected client
5. **Message Queue Management**: Buffering messages during disconnection

## Implementation Strategies

### Session Token-Based Migration
The client receives a unique session token upon initial connection, which is used to resume the session after reconnection.

### Message Sequence Numbers
Both client and server track message sequence numbers to identify and retransmit any lost messages.

### Exponential Backoff
Implementing increasingly longer delays between reconnection attempts to avoid overwhelming the server.

## C/C++ Implementation

Here's a robust WebSocket connection migration implementation using libwebsockets:

```c
#include <libwebsockets.h>
#include <string>
#include <queue>
#include <chrono>
#include <thread>
#include <mutex>
#include <iostream>

// Connection state management
enum ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING
};

// Message structure for queuing
struct QueuedMessage {
    uint64_t sequence_num;
    std::string payload;
    std::chrono::steady_clock::time_point timestamp;
};

class WebSocketMigration {
private:
    struct lws_context* context;
    struct lws* wsi;
    ConnectionState state;
    std::string session_token;
    uint64_t next_sequence_num;
    uint64_t last_acked_sequence;
    std::queue<QueuedMessage> pending_messages;
    std::mutex queue_mutex;
    
    // Reconnection parameters
    int reconnect_attempts;
    int max_reconnect_attempts;
    std::chrono::milliseconds base_backoff;
    std::chrono::steady_clock::time_point last_connect_attempt;
    
    // Connection quality tracking
    std::chrono::steady_clock::time_point last_pong_received;
    int missed_pongs;
    const int max_missed_pongs = 3;

public:
    WebSocketMigration() 
        : context(nullptr), 
          wsi(nullptr),
          state(DISCONNECTED),
          next_sequence_num(0),
          last_acked_sequence(0),
          reconnect_attempts(0),
          max_reconnect_attempts(10),
          base_backoff(std::chrono::milliseconds(1000)),
          missed_pongs(0) {}
    
    // Calculate exponential backoff delay
    std::chrono::milliseconds calculate_backoff() {
        int exponent = std::min(reconnect_attempts, 6); // Cap at 2^6 = 64x
        return base_backoff * (1 << exponent); // Exponential: 1s, 2s, 4s, 8s...
    }
    
    // Initialize WebSocket connection with session token
    bool connect(const char* address, int port, const char* path) {
        struct lws_context_creation_info info;
        memset(&info, 0, sizeof(info));
        
        info.port = CONTEXT_PORT_NO_LISTEN;
        info.protocols = protocols;
        info.gid = -1;
        info.uid = -1;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        
        context = lws_create_context(&info);
        if (!context) {
            std::cerr << "Failed to create context" << std::endl;
            return false;
        }
        
        struct lws_client_connect_info ccinfo;
        memset(&ccinfo, 0, sizeof(ccinfo));
        
        ccinfo.context = context;
        ccinfo.address = address;
        ccinfo.port = port;
        ccinfo.path = path;
        ccinfo.host = address;
        ccinfo.origin = address;
        ccinfo.protocol = protocols[0].name;
        ccinfo.userdata = this;
        
        state = CONNECTING;
        wsi = lws_client_connect_via_info(&ccinfo);
        last_connect_attempt = std::chrono::steady_clock::now();
        
        return wsi != nullptr;
    }
    
    // Handle reconnection with backoff
    bool attempt_reconnect(const char* address, int port, const char* path) {
        auto now = std::chrono::steady_clock::now();
        auto time_since_last = now - last_connect_attempt;
        auto backoff = calculate_backoff();
        
        if (time_since_last < backoff) {
            // Not enough time has passed
            return false;
        }
        
        if (reconnect_attempts >= max_reconnect_attempts) {
            std::cerr << "Max reconnection attempts reached" << std::endl;
            state = DISCONNECTED;
            return false;
        }
        
        std::cout << "Reconnection attempt " << (reconnect_attempts + 1) 
                  << " after " << backoff.count() << "ms" << std::endl;
        
        reconnect_attempts++;
        state = RECONNECTING;
        
        return connect(address, port, path);
    }
    
    // Send message with sequence number
    bool send_message(const std::string& message) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        QueuedMessage qmsg;
        qmsg.sequence_num = next_sequence_num++;
        qmsg.payload = message;
        qmsg.timestamp = std::chrono::steady_clock::now();
        
        if (state == CONNECTED) {
            // Send immediately
            std::string frame = build_message_frame(qmsg);
            int n = lws_write(wsi, 
                            (unsigned char*)frame.c_str() + LWS_PRE,
                            frame.length(), 
                            LWS_WRITE_TEXT);
            
            if (n < 0) {
                // Failed to send, queue it
                pending_messages.push(qmsg);
                return false;
            }
            return true;
        } else {
            // Queue for later
            pending_messages.push(qmsg);
            return false;
        }
    }
    
    // Build message with metadata for migration
    std::string build_message_frame(const QueuedMessage& msg) {
        // Format: SEQ:TOKEN:PAYLOAD
        return std::to_string(msg.sequence_num) + ":" + 
               session_token + ":" + msg.payload;
    }
    
    // Flush queued messages after reconnection
    void flush_pending_messages() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        while (!pending_messages.empty()) {
            QueuedMessage& msg = pending_messages.front();
            
            std::string frame = build_message_frame(msg);
            int n = lws_write(wsi,
                            (unsigned char*)frame.c_str() + LWS_PRE,
                            frame.length(),
                            LWS_WRITE_TEXT);
            
            if (n < 0) {
                // Failed, stop trying
                break;
            }
            
            pending_messages.pop();
        }
    }
    
    // Handle connection establishment
    void on_connected() {
        state = CONNECTED;
        reconnect_attempts = 0;
        missed_pongs = 0;
        last_pong_received = std::chrono::steady_clock::now();
        
        std::cout << "Connection established" << std::endl;
        
        // Request session token if first connection
        if (session_token.empty()) {
            send_control_message("REQUEST_SESSION");
        } else {
            // Resume session
            send_control_message("RESUME_SESSION:" + session_token);
            flush_pending_messages();
        }
    }
    
    // Handle incoming messages
    void on_message_received(const char* data, size_t len) {
        std::string message(data, len);
        
        // Parse message: TYPE:DATA
        size_t delimiter = message.find(':');
        if (delimiter != std::string::npos) {
            std::string msg_type = message.substr(0, delimiter);
            std::string msg_data = message.substr(delimiter + 1);
            
            if (msg_type == "SESSION_TOKEN") {
                session_token = msg_data;
                std::cout << "Received session token: " << session_token << std::endl;
            } else if (msg_type == "ACK") {
                uint64_t ack_seq = std::stoull(msg_data);
                last_acked_sequence = ack_seq;
                // Remove acknowledged messages from queue
                cleanup_acked_messages(ack_seq);
            } else if (msg_type == "PONG") {
                last_pong_received = std::chrono::steady_clock::now();
                missed_pongs = 0;
            }
        }
    }
    
    // Send control messages
    void send_control_message(const std::string& msg) {
        int n = lws_write(wsi,
                        (unsigned char*)msg.c_str() + LWS_PRE,
                        msg.length(),
                        LWS_WRITE_TEXT);
        if (n < 0) {
            std::cerr << "Failed to send control message" << std::endl;
        }
    }
    
    // Monitor connection health
    void check_connection_health() {
        auto now = std::chrono::steady_clock::now();
        auto time_since_pong = now - last_pong_received;
        
        if (time_since_pong > std::chrono::seconds(30)) {
            missed_pongs++;
            if (missed_pongs >= max_missed_pongs) {
                std::cout << "Connection appears dead, triggering reconnect" << std::endl;
                on_disconnected();
            } else {
                // Send ping
                send_control_message("PING");
            }
        }
    }
    
    // Handle disconnection
    void on_disconnected() {
        state = RECONNECTING;
        wsi = nullptr;
        std::cout << "Connection lost, will attempt reconnection" << std::endl;
    }
    
    // Cleanup acknowledged messages
    void cleanup_acked_messages(uint64_t ack_seq) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        while (!pending_messages.empty() && 
               pending_messages.front().sequence_num <= ack_seq) {
            pending_messages.pop();
        }
    }
    
    ConnectionState get_state() const { return state; }
    size_t pending_message_count() const { return pending_messages.size(); }
    
private:
    static struct lws_protocols protocols[];
};

// Protocol definition
struct lws_protocols WebSocketMigration::protocols[] = {
    {
        "migration-protocol",
        [](struct lws *wsi, enum lws_callback_reasons reason,
           void *user, void *in, size_t len) -> int {
            WebSocketMigration* client = 
                (WebSocketMigration*)lws_context_user(lws_get_context(wsi));
            
            switch (reason) {
                case LWS_CALLBACK_CLIENT_ESTABLISHED:
                    client->on_connected();
                    break;
                    
                case LWS_CALLBACK_CLIENT_RECEIVE:
                    client->on_message_received((const char*)in, len);
                    break;
                    
                case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
                case LWS_CALLBACK_CLOSED:
                    client->on_disconnected();
                    break;
                    
                default:
                    break;
            }
            return 0;
        },
        0,
        0,
    },
    { NULL, NULL, 0, 0 }
};

// Example usage
int main() {
    WebSocketMigration client;
    
    // Initial connection
    if (!client.connect("echo.websocket.org", 443, "/")) {
        std::cerr << "Initial connection failed" << std::endl;
        return 1;
    }
    
    // Main loop
    while (true) {
        // Service the connection
        if (client.get_state() == CONNECTED) {
            client.send_message("Hello from migrating client!");
            client.check_connection_health();
        } else if (client.get_state() == RECONNECTING) {
            client.attempt_reconnect("echo.websocket.org", 443, "/");
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    return 0;
}
```

## Rust Implementation

Rust provides excellent async/await support and type safety for connection migration:

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message, WebSocketStream};
use tokio_tungstenite::tungstenite::protocol::CloseFrame;
use futures_util::{SinkExt, StreamExt};
use tokio::time::{sleep, Duration, Instant};
use std::collections::VecDeque;
use std::sync::Arc;
use tokio::sync::Mutex;
use serde::{Deserialize, Serialize};
use url::Url;

// Connection state enum
#[derive(Debug, Clone, PartialEq)]
enum ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
}

// Queued message with metadata
#[derive(Debug, Clone, Serialize, Deserialize)]
struct QueuedMessage {
    sequence_num: u64,
    payload: String,
    timestamp: u64,
}

// Control message types
#[derive(Debug, Serialize, Deserialize)]
enum ControlMessage {
    RequestSession,
    ResumeSession { token: String, last_seq: u64 },
    SessionToken { token: String },
    Ack { sequence: u64 },
    Ping,
    Pong,
}

// Main WebSocket migration client
struct WebSocketMigration {
    url: String,
    state: Arc<Mutex<ConnectionState>>,
    session_token: Arc<Mutex<Option<String>>>,
    next_sequence_num: Arc<Mutex<u64>>,
    last_acked_sequence: Arc<Mutex<u64>>,
    pending_messages: Arc<Mutex<VecDeque<QueuedMessage>>>,
    reconnect_attempts: Arc<Mutex<u32>>,
    max_reconnect_attempts: u32,
    base_backoff_ms: u64,
    last_pong: Arc<Mutex<Instant>>,
    missed_pongs: Arc<Mutex<u32>>,
}

impl WebSocketMigration {
    fn new(url: String) -> Self {
        Self {
            url,
            state: Arc::new(Mutex::new(ConnectionState::Disconnected)),
            session_token: Arc::new(Mutex::new(None)),
            next_sequence_num: Arc::new(Mutex::new(0)),
            last_acked_sequence: Arc::new(Mutex::new(0)),
            pending_messages: Arc::new(Mutex::new(VecDeque::new())),
            reconnect_attempts: Arc::new(Mutex::new(0)),
            max_reconnect_attempts: 10,
            base_backoff_ms: 1000,
            last_pong: Arc::new(Mutex::new(Instant::now())),
            missed_pongs: Arc::new(Mutex::new(0)),
        }
    }
    
    // Calculate exponential backoff delay
    async fn calculate_backoff(&self) -> Duration {
        let attempts = *self.reconnect_attempts.lock().await;
        let exponent = std::cmp::min(attempts, 6); // Cap at 2^6 = 64x
        let multiplier = 2u64.pow(exponent);
        Duration::from_millis(self.base_backoff_ms * multiplier)
    }
    
    // Connect to WebSocket server
    async fn connect(&self) -> Result<(), Box<dyn std::error::Error>> {
        *self.state.lock().await = ConnectionState::Connecting;
        
        let url = Url::parse(&self.url)?;
        let (ws_stream, _) = connect_async(url).await?;
        
        println!("WebSocket connected successfully");
        *self.state.lock().await = ConnectionState::Connected;
        *self.reconnect_attempts.lock().await = 0;
        *self.last_pong.lock().await = Instant::now();
        *self.missed_pongs.lock().await = 0;
        
        // Handle the connection
        self.handle_connection(ws_stream).await?;
        
        Ok(())
    }
    
    // Main connection handler
    async fn handle_connection(
        &self,
        ws_stream: WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>
    ) -> Result<(), Box<dyn std::error::Error>> {
        let (mut write, mut read) = ws_stream.split();
        
        // Request or resume session
        let control_msg = if self.session_token.lock().await.is_none() {
            ControlMessage::RequestSession
        } else {
            let token = self.session_token.lock().await.clone().unwrap();
            let last_seq = *self.last_acked_sequence.lock().await;
            ControlMessage::ResumeSession { token, last_seq }
        };
        
        let msg_json = serde_json::to_string(&control_msg)?;
        write.send(Message::Text(msg_json)).await?;
        
        // Flush pending messages if resuming
        if self.session_token.lock().await.is_some() {
            self.flush_pending_messages(&mut write).await?;
        }
        
        // Start health check task
        let health_checker = self.clone_for_task();
        let health_handle = tokio::spawn(async move {
            health_checker.health_check_loop().await;
        });
        
        // Message processing loop
        while let Some(message) = read.next().await {
            match message {
                Ok(Message::Text(text)) => {
                    self.handle_message(&text, &mut write).await?;
                }
                Ok(Message::Close(_)) => {
                    println!("Connection closed by server");
                    break;
                }
                Ok(Message::Ping(data)) => {
                    write.send(Message::Pong(data)).await?;
                }
                Err(e) => {
                    eprintln!("Error receiving message: {}", e);
                    break;
                }
                _ => {}
            }
        }
        
        health_handle.abort();
        *self.state.lock().await = ConnectionState::Reconnecting;
        
        Ok(())
    }
    
    // Handle incoming messages
    async fn handle_message(
        &self,
        text: &str,
        write: &mut futures_util::stream::SplitSink<
            WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>,
            Message
        >
    ) -> Result<(), Box<dyn std::error::Error>> {
        // Try to parse as control message
        if let Ok(control) = serde_json::from_str::<ControlMessage>(text) {
            match control {
                ControlMessage::SessionToken { token } => {
                    println!("Received session token: {}", token);
                    *self.session_token.lock().await = Some(token);
                }
                ControlMessage::Ack { sequence } => {
                    *self.last_acked_sequence.lock().await = sequence;
                    self.cleanup_acked_messages(sequence).await;
                }
                ControlMessage::Pong => {
                    *self.last_pong.lock().await = Instant::now();
                    *self.missed_pongs.lock().await = 0;
                }
                ControlMessage::Ping => {
                    let pong = serde_json::to_string(&ControlMessage::Pong)?;
                    write.send(Message::Text(pong)).await?;
                }
                _ => {}
            }
        } else {
            // Handle regular application messages
            println!("Received: {}", text);
        }
        
        Ok(())
    }
    
    // Send a message with sequence tracking
    async fn send_message(&self, payload: String) -> Result<(), Box<dyn std::error::Error>> {
        let mut seq_num = self.next_sequence_num.lock().await;
        let sequence = *seq_num;
        *seq_num += 1;
        drop(seq_num);
        
        let queued_msg = QueuedMessage {
            sequence_num: sequence,
            payload,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?
                .as_secs(),
        };
        
        // Add to pending queue
        self.pending_messages.lock().await.push_back(queued_msg.clone());
        
        // Try to send if connected
        if *self.state.lock().await == ConnectionState::Connected {
            // In real implementation, would send through the active connection
            println!("Sending message seq {}: {}", sequence, queued_msg.payload);
        } else {
            println!("Queued message seq {} for later delivery", sequence);
        }
        
        Ok(())
    }
    
    // Flush pending messages after reconnection
    async fn flush_pending_messages(
        &self,
        write: &mut futures_util::stream::SplitSink<
            WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>,
            Message
        >
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut pending = self.pending_messages.lock().await;
        
        println!("Flushing {} pending messages", pending.len());
        
        while let Some(msg) = pending.pop_front() {
            let json = serde_json::to_string(&msg)?;
            if let Err(e) = write.send(Message::Text(json)).await {
                eprintln!("Failed to send pending message: {}", e);
                // Put it back
                pending.push_front(msg);
                break;
            }
        }
        
        Ok(())
    }
    
    // Health check loop
    async fn health_check_loop(&self) {
        let mut interval = tokio::time::interval(Duration::from_secs(10));
        
        loop {
            interval.tick().await;
            
            let time_since_pong = self.last_pong.lock().await.elapsed();
            
            if time_since_pong > Duration::from_secs(30) {
                let mut missed = self.missed_pongs.lock().await;
                *missed += 1;
                
                if *missed >= 3 {
                    println!("Connection appears dead, triggering reconnect");
                    *self.state.lock().await = ConnectionState::Reconnecting;
                    break;
                }
            }
        }
    }
    
    // Cleanup acknowledged messages from queue
    async fn cleanup_acked_messages(&self, ack_seq: u64) {
        let mut pending = self.pending_messages.lock().await;
        
        while let Some(msg) = pending.front() {
            if msg.sequence_num <= ack_seq {
                pending.pop_front();
            } else {
                break;
            }
        }
        
        println!("Cleaned up messages up to seq {}. {} remaining", 
                 ack_seq, pending.len());
    }
    
    // Reconnection loop with exponential backoff
    async fn reconnect_loop(&self) {
        loop {
            let state = *self.state.lock().await;
            
            match state {
                ConnectionState::Reconnecting | ConnectionState::Disconnected => {
                    let attempts = *self.reconnect_attempts.lock().await;
                    
                    if attempts >= self.max_reconnect_attempts {
                        eprintln!("Max reconnection attempts reached");
                        *self.state.lock().await = ConnectionState::Disconnected;
                        break;
                    }
                    
                    let backoff = self.calculate_backoff().await;
                    println!("Reconnecting in {:?} (attempt {})", backoff, attempts + 1);
                    
                    sleep(backoff).await;
                    *self.reconnect_attempts.lock().await += 1;
                    
                    if let Err(e) = self.connect().await {
                        eprintln!("Reconnection failed: {}", e);
                    }
                }
                ConnectionState::Connected => {
                    // Connection is healthy
                    break;
                }
                _ => {
                    sleep(Duration::from_secs(1)).await;
                }
            }
        }
    }
    
    // Helper to clone for async tasks
    fn clone_for_task(&self) -> Self {
        Self {
            url: self.url.clone(),
            state: Arc::clone(&self.state),
            session_token: Arc::clone(&self.session_token),
            next_sequence_num: Arc::clone(&self.next_sequence_num),
            last_acked_sequence: Arc::clone(&self.last_acked_sequence),
            pending_messages: Arc::clone(&self.pending_messages),
            reconnect_attempts: Arc::clone(&self.reconnect_attempts),
            max_reconnect_attempts: self.max_reconnect_attempts,
            base_backoff_ms: self.base_backoff_ms,
            last_pong: Arc::clone(&self.last_pong),
            missed_pongs: Arc::clone(&self.missed_pongs),
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = WebSocketMigration::new("ws://echo.websocket.org".to_string());
    
    // Start initial connection
    let connect_client = client.clone_for_task();
    let connect_handle = tokio::spawn(async move {
        if let Err(e) = connect_client.connect().await {
            eprintln!("Connection error: {}", e);
        }
    });
    
    // Wait a bit for connection
    sleep(Duration::from_secs(2)).await;
    
    // Send some messages
    for i in 0..5 {
        client.send_message(format!("Message {}", i)).await?;
        sleep(Duration::from_secs(1)).await;
    }
    
    // Start reconnection loop
    let reconnect_client = client.clone_for_task();
    let reconnect_handle = tokio::spawn(async move {
        reconnect_client.reconnect_loop().await;
    });
    
    // Wait for completion
    connect_handle.await?;
    reconnect_handle.await?;
    
    Ok(())
}
```

## Key Implementation Details

### 1. **Session Token Management**
Both implementations use session tokens to identify clients across reconnections. When a client first connects, it requests a token from the server. On subsequent reconnections, it presents this token to resume its session.

### 2. **Message Sequencing**
Every message is assigned a monotonically increasing sequence number. This allows both client and server to:
- Identify missing messages
- Prevent duplicate processing
- Retransmit lost messages in order

### 3. **Exponential Backoff**
The reconnection delay doubles with each failed attempt (capped at a maximum), preventing overwhelming the server during network issues:
- Attempt 1: 1 second
- Attempt 2: 2 seconds
- Attempt 3: 4 seconds
- Attempt 4: 8 seconds
- And so on...

### 4. **Connection Health Monitoring**
Both implementations use ping/pong heartbeats to detect connection degradation before the OS reports the connection as closed. This enables proactive reconnection.

### 5. **Message Queue Management**
Messages sent while disconnected are queued in memory and flushed upon reconnection. The queue is pruned based on acknowledgments from the server to prevent unbounded growth.

## Summary

**Connection Migration** in WebSockets ensures continuous service during network disruptions through intelligent reconnection strategies. The key components are:

- **Session continuity**: Using tokens to maintain identity across connections
- **Message reliability**: Sequence numbers and queuing prevent data loss
- **Smart reconnection**: Exponential backoff prevents server overload
- **Health monitoring**: Proactive detection of connection issues
- **State preservation**: Maintaining application state during transitions

The C/C++ implementation using libwebsockets provides low-level control and high performance, while the Rust implementation leverages async/await for cleaner concurrency patterns and strong type safety. Both approaches handle the complexities of network failures gracefully, providing users with a seamless experience even in challenging network conditions.