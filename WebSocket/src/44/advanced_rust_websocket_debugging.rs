// Advanced debugging features for WebSocket applications
// Cargo.toml dependencies:
// [dependencies]
// tokio = { version = "1", features = ["full", "tracing"] }
// tokio-tungstenite = "0.21"
// tracing = "0.1"
// tracing-subscriber = { version = "0.3", features = ["env-filter", "json"] }
// serde = { version = "1", features = ["derive"] }
// serde_json = "1"

use tokio::net::TcpListener;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tracing::{debug, error, info, warn, span, Level};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant};
use serde::{Deserialize, Serialize};

/// Connection statistics for debugging
#[derive(Debug, Clone, Serialize)]
struct ConnectionStats {
    messages_received: u64,
    messages_sent: u64,
    bytes_received: u64,
    bytes_sent: u64,
    errors: u64,
    connection_duration: Duration,
}

impl ConnectionStats {
    fn new() -> Self {
        Self {
            messages_received: 0,
            messages_sent: 0,
            bytes_received: 0,
            bytes_sent: 0,
            errors: 0,
            connection_duration: Duration::from_secs(0),
        }
    }
}

/// Global server statistics
struct ServerStats {
    total_connections: AtomicU64,
    active_connections: AtomicU64,
    total_messages: AtomicU64,
}

impl ServerStats {
    fn new() -> Self {
        Self {
            total_connections: AtomicU64::new(0),
            active_connections: AtomicU64::new(0),
            total_messages: AtomicU64::new(0),
        }
    }
    
    fn connection_opened(&self) {
        self.total_connections.fetch_add(1, Ordering::Relaxed);
        self.active_connections.fetch_add(1, Ordering::Relaxed);
    }
    
    fn connection_closed(&self) {
        self.active_connections.fetch_sub(1, Ordering::Relaxed);
    }
    
    fn message_received(&self) {
        self.total_messages.fetch_add(1, Ordering::Relaxed);
    }
    
    fn log_stats(&self) {
        info!(
            total_connections = self.total_connections.load(Ordering::Relaxed),
            active_connections = self.active_connections.load(Ordering::Relaxed),
            total_messages = self.total_messages.load(Ordering::Relaxed),
            "Server statistics"
        );
    }
}

/// Debug wrapper for WebSocket messages
#[derive(Debug, Serialize)]
struct MessageDebugInfo {
    #[serde(rename = "type")]
    msg_type: String,
    size: usize,
    timestamp: String,
    payload_preview: String,
}

impl MessageDebugInfo {
    fn from_message(msg: &Message) -> Self {
        let msg_type = match msg {
            Message::Text(_) => "text",
            Message::Binary(_) => "binary",
            Message::Ping(_) => "ping",
            Message::Pong(_) => "pong",
            Message::Close(_) => "close",
            Message::Frame(_) => "frame",
        }.to_string();
        
        let (size, payload_preview) = match msg {
            Message::Text(text) => {
                let preview = if text.len() > 50 {
                    format!("{}...", &text[..50])
                } else {
                    text.clone()
                };
                (text.len(), preview)
            }
            Message::Binary(data) => {
                let preview: Vec<String> = data.iter()
                    .take(16)
                    .map(|b| format!("{:02x}", b))
                    .collect();
                (data.len(), format!("[{}]", preview.join(" ")))
            }
            Message::Ping(data) | Message::Pong(data) => {
                (data.len(), format!("{} bytes", data.len()))
            }
            Message::Close(frame) => {
                let preview = frame.as_ref()
                    .map(|f| format!("code: {}, reason: {}", f.code, f.reason))
                    .unwrap_or_else(|| "no details".to_string());
                (0, preview)
            }
            Message::Frame(_) => (0, "raw frame".to_string()),
        };
        
        Self {
            msg_type,
            size,
            timestamp: chrono::Utc::now().to_rfc3339(),
            payload_preview,
        }
    }
}

async fn handle_connection_with_stats(
    stream: tokio::net::TcpStream,
    addr: std::net::SocketAddr,
    server_stats: Arc<ServerStats>,
) {
    let conn_span = span!(Level::INFO, "connection", %addr);
    let _enter = conn_span.enter();
    
    let start_time = Instant::now();
    let mut stats = ConnectionStats::new();
    
    server_stats.connection_opened();
    info!("Connection opened");
    
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            error!(error = ?e, "Handshake failed");
            server_stats.connection_closed();
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn periodic stats logger
    let stats_task = {
        let addr = addr;
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(30));
            loop {
                interval.tick().await;
                debug!(
                    messages_rx = stats.messages_received,
                    messages_tx = stats.messages_sent,
                    bytes_rx = stats.bytes_received,
                    bytes_tx = stats.bytes_sent,
                    errors = stats.errors,
                    "Periodic stats for {}",
                    addr
                );
            }
        })
    };
    
    while let Some(msg_result) = read.next().await {
        match msg_result {
            Ok(msg) => {
                // Log detailed message information
                let debug_info = MessageDebugInfo::from_message(&msg);
                debug!(
                    msg_type = %debug_info.msg_type,
                    size = debug_info.size,
                    preview = %debug_info.payload_preview,
                    "Received message"
                );
                
                stats.messages_received += 1;
                server_stats.message_received();
                
                match &msg {
                    Message::Text(text) => {
                        stats.bytes_received += text.len() as u64;
                        
                        // Echo with debug metadata
                        let response = Message::Text(format!(
                            "{{\"echo\":\"{}\",\"stats\":{}}}",
                            text,
                            serde_json::to_string(&stats).unwrap_or_default()
                        ));
                        
                        if let Err(e) = write.send(response).await {
                            error!(error = ?e, "Send failed");
                            stats.errors += 1;
                            break;
                        }
                        
                        stats.messages_sent += 1;
                        stats.bytes_sent += text.len() as u64;
                    }
                    
                    Message::Binary(data) => {
                        stats.bytes_received += data.len() as u64;
                        
                        if let Err(e) = write.send(Message::Binary(data.clone())).await {
                            error!(error = ?e, "Binary send failed");
                            stats.errors += 1;
                            break;
                        }
                        
                        stats.messages_sent += 1;
                        stats.bytes_sent += data.len() as u64;
                    }
                    
                    Message::Ping(data) => {
                        debug!("Responding to ping");
                        if let Err(e) = write.send(Message::Pong(data.clone())).await {
                            error!(error = ?e, "Pong send failed");
                            stats.errors += 1;
                        }
                    }
                    
                    Message::Close(frame) => {
                        if let Some(cf) = frame {
                            info!(code = cf.code.into(), reason = %cf.reason, "Close received");
                        }
                        let _ = write.send(Message::Close(None)).await;
                        break;
                    }
                    
                    _ => {}
                }
            }
            Err(e) => {
                error!(error = ?e, "Message receive error");
                stats.errors += 1;
                break;
            }
        }
    }
    
    stats_task.abort();
    
    stats.connection_duration = start_time.elapsed();
    server_stats.connection_closed();
    
    info!(
        duration_secs = stats.connection_duration.as_secs(),
        messages_rx = stats.messages_received,
        messages_tx = stats.messages_sent,
        bytes_rx = stats.bytes_received,
        bytes_tx = stats.bytes_sent,
        errors = stats.errors,
        "Connection closed"
    );
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize JSON tracing for structured logging
    tracing_subscriber::fmt()
        .with_target(true)
        .with_thread_ids(true)
        .with_file(true)
        .with_line_number(true)
        .json()
        .init();
    
    let server_stats = Arc::new(ServerStats::new());
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    
    info!(address = %addr, "Server started");
    
    // Spawn periodic server stats logger
    let stats_clone = Arc::clone(&server_stats);
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_secs(60));
        loop {
            interval.tick().await;
            stats_clone.log_stats();
        }
    });
    
    while let Ok((stream, addr)) = listener.accept().await {
        let stats = Arc::clone(&server_stats);
        tokio::spawn(async move {
            handle_connection_with_stats(stream, addr, stats).await;
        });
    }
    
    Ok(())
}