// Cargo.toml dependencies:
// [dependencies]
// tokio = { version = "1", features = ["full"] }
// tokio-tungstenite = "0.21"
// tracing = "0.1"
// tracing-subscriber = { version = "0.3", features = ["env-filter"] }
// futures-util = "0.3"

use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tracing::{debug, error, info, warn, instrument, Level};
use tracing_subscriber::{fmt, layer::SubscriberExt, util::SubscriberInitExt};
use std::net::SocketAddr;

#[instrument(skip(stream))]
async fn handle_connection(stream: TcpStream, addr: SocketAddr) {
    info!("New connection attempt from {}", addr);
    
    // Perform WebSocket handshake
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => {
            info!("WebSocket handshake successful with {}", addr);
            ws
        }
        Err(e) => {
            error!("WebSocket handshake failed with {}: {:?}", addr, e);
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    debug!("Split WebSocket stream for {}", addr);
    
    // Message counter for debugging
    let mut msg_count = 0u64;
    
    while let Some(msg) = read.next().await {
        match msg {
            Ok(msg) => {
                msg_count += 1;
                
                match &msg {
                    Message::Text(text) => {
                        debug!(
                            count = msg_count,
                            length = text.len(),
                            "Received text message from {}: {}",
                            addr,
                            text
                        );
                        
                        // Echo back with debug info
                        let response = format!("Echo #{}: {}", msg_count, text);
                        
                        if let Err(e) = write.send(Message::Text(response)).await {
                            error!("Failed to send response to {}: {:?}", addr, e);
                            break;
                        }
                        
                        debug!("Successfully echoed message #{} to {}", msg_count, addr);
                    }
                    
                    Message::Binary(data) => {
                        debug!(
                            count = msg_count,
                            length = data.len(),
                            "Received binary message from {}",
                            addr
                        );
                        
                        // Log first few bytes for debugging
                        let preview: Vec<String> = data.iter()
                            .take(16)
                            .map(|b| format!("{:02x}", b))
                            .collect();
                        debug!("Binary data preview: {}", preview.join(" "));
                        
                        if let Err(e) = write.send(Message::Binary(data.clone())).await {
                            error!("Failed to send binary response to {}: {:?}", addr, e);
                            break;
                        }
                    }
                    
                    Message::Ping(data) => {
                        debug!(
                            count = msg_count,
                            length = data.len(),
                            "Received ping from {}",
                            addr
                        );
                        
                        if let Err(e) = write.send(Message::Pong(data.clone())).await {
                            error!("Failed to send pong to {}: {:?}", addr, e);
                            break;
                        }
                        
                        debug!("Sent pong response to {}", addr);
                    }
                    
                    Message::Pong(data) => {
                        debug!(
                            count = msg_count,
                            length = data.len(),
                            "Received pong from {}",
                            addr
                        );
                    }
                    
                    Message::Close(frame) => {
                        if let Some(cf) = frame {
                            info!(
                                code = cf.code.into(),
                                reason = %cf.reason,
                                "Received close frame from {}",
                                addr
                            );
                        } else {
                            info!("Received close frame (no details) from {}", addr);
                        }
                        
                        debug!("Sending close acknowledgment to {}", addr);
                        let _ = write.send(Message::Close(None)).await;
                        break;
                    }
                    
                    Message::Frame(_) => {
                        warn!("Received raw frame from {} (unexpected)", addr);
                    }
                }
            }
            Err(e) => {
                error!("Error receiving message from {}: {:?}", addr, e);
                break;
            }
        }
    }
    
    info!(
        total_messages = msg_count,
        "Connection closed with {}",
        addr
    );
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing with environment filter
    // Set RUST_LOG environment variable to control log level
    // Examples:
    //   RUST_LOG=debug cargo run
    //   RUST_LOG=websocket_server=trace cargo run
    tracing_subscriber::registry()
        .with(fmt::layer().with_target(true).with_thread_ids(true))
        .with(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "debug".into())
        )
        .init();
    
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    
    info!("WebSocket server listening on {}", addr);
    info!("Set RUST_LOG environment variable to control verbosity");
    info!("Example: RUST_LOG=trace cargo run");
    
    while let Ok((stream, addr)) = listener.accept().await {
        debug!("Accepted TCP connection from {}", addr);
        
        // Spawn a new task for each connection
        tokio::spawn(async move {
            handle_connection(stream, addr).await;
        });
    }
    
    Ok(())
}

// Example client for testing
#[cfg(test)]
mod tests {
    use super::*;
    use tokio_tungstenite::connect_async;
    
    #[tokio::test]
    async fn test_websocket_echo() {
        // This would connect to a running server
        let url = "ws://127.0.0.1:8080";
        
        match connect_async(url).await {
            Ok((mut ws_stream, _)) => {
                info!("Test client connected");
                
                // Send test message
                ws_stream.send(Message::Text("Hello".into())).await.unwrap();
                
                // Receive echo
                if let Some(msg) = ws_stream.next().await {
                    match msg {
                        Ok(Message::Text(text)) => {
                            debug!("Received: {}", text);
                            assert!(text.contains("Hello"));
                        }
                        _ => panic!("Expected text message"),
                    }
                }
            }
            Err(e) => {
                error!("Connection failed: {:?}", e);
            }
        }
    }
}