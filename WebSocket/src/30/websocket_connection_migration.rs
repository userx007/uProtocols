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