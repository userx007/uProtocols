use tokio::net::{TcpListener, TcpStream};
use tokio::time::{interval, timeout, Duration, Instant};
use tokio_tungstenite::{accept_async, tungstenite::Message, WebSocketStream};
use futures_util::{StreamExt, SinkExt};
use std::sync::Arc;

const PING_INTERVAL: Duration = Duration::from_secs(30);
const PONG_TIMEOUT: Duration = Duration::from_secs(10);

struct ConnectionState {
    last_pong_received: Instant,
    waiting_for_pong: bool,
}

impl ConnectionState {
    fn new() -> Self {
        Self {
            last_pong_received: Instant::now(),
            waiting_for_pong: false,
        }
    }
}

async fn handle_connection(stream: TcpStream) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    println!("WebSocket connection established");
    
    let (mut write, mut read) = ws_stream.split();
    let mut state = ConnectionState::new();
    let mut ping_interval = interval(PING_INTERVAL);
    ping_interval.tick().await; // First tick completes immediately
    
    loop {
        tokio::select! {
            // Handle incoming messages
            msg = read.next() => {
                match msg {
                    Some(Ok(message)) => {
                        match message {
                            Message::Text(text) => {
                                println!("Received text: {}", text);
                                // Echo back or handle the message
                                write.send(Message::Text(text)).await?;
                            }
                            Message::Binary(data) => {
                                println!("Received binary data: {} bytes", data.len());
                                write.send(Message::Binary(data)).await?;
                            }
                            Message::Ping(payload) => {
                                println!("Received ping, sending pong");
                                // tungstenite automatically sends pong, but we can do it manually
                                write.send(Message::Pong(payload)).await?;
                            }
                            Message::Pong(_payload) => {
                                println!("Received pong response");
                                state.last_pong_received = Instant::now();
                                state.waiting_for_pong = false;
                            }
                            Message::Close(frame) => {
                                println!("Received close frame: {:?}", frame);
                                write.send(Message::Close(None)).await?;
                                break;
                            }
                            _ => {}
                        }
                    }
                    Some(Err(e)) => {
                        eprintln!("WebSocket error: {}", e);
                        break;
                    }
                    None => {
                        println!("Connection closed by client");
                        break;
                    }
                }
            }
            
            // Send periodic pings
            _ = ping_interval.tick() => {
                // Check for pong timeout
                if state.waiting_for_pong {
                    let elapsed = state.last_pong_received.elapsed();
                    if elapsed > PONG_TIMEOUT {
                        eprintln!("Pong timeout exceeded - closing connection");
                        write.send(Message::Close(None)).await?;
                        break;
                    }
                }
                
                // Send ping with optional payload
                let ping_payload = format!("ping-{}", Instant::now().elapsed().as_secs());
                println!("Sending ping frame");
                
                match write.send(Message::Ping(ping_payload.into_bytes())).await {
                    Ok(_) => {
                        state.waiting_for_pong = true;
                    }
                    Err(e) => {
                        eprintln!("Failed to send ping: {}", e);
                        break;
                    }
                }
            }
        }
    }
    
    println!("Connection handler finished");
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on: {}", addr);
    
    while let Ok((stream, peer_addr)) = listener.accept().await {
        println!("New connection from: {}", peer_addr);
        
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream).await {
                eprintln!("Error handling connection: {}", e);
            }
        });
    }
    
    Ok(())
}

// Client example with ping-pong handling
#[cfg(test)]
mod client_example {
    use super::*;
    use tokio_tungstenite::connect_async;
    
    pub async fn websocket_client() -> Result<(), Box<dyn std::error::Error>> {
        let url = "ws://127.0.0.1:8080";
        let (ws_stream, _) = connect_async(url).await?;
        println!("Connected to server");
        
        let (mut write, mut read) = ws_stream.split();
        let mut ping_interval = interval(Duration::from_secs(20));
        let mut last_pong = Instant::now();
        
        loop {
            tokio::select! {
                msg = read.next() => {
                    match msg {
                        Some(Ok(Message::Pong(_))) => {
                            println!("Client received pong");
                            last_pong = Instant::now();
                        }
                        Some(Ok(Message::Ping(payload))) => {
                            println!("Client received ping");
                            write.send(Message::Pong(payload)).await?;
                        }
                        Some(Ok(Message::Text(text))) => {
                            println!("Client received: {}", text);
                        }
                        Some(Ok(Message::Close(_))) => {
                            println!("Server closed connection");
                            break;
                        }
                        Some(Err(e)) => {
                            eprintln!("Error: {}", e);
                            break;
                        }
                        None => break,
                        _ => {}
                    }
                }
                
                _ = ping_interval.tick() => {
                    // Check if we haven't received a pong in too long
                    if last_pong.elapsed() > Duration::from_secs(40) {
                        eprintln!("No pong received - connection dead");
                        break;
                    }
                    
                    println!("Client sending ping");
                    write.send(Message::Ping(vec![])).await?;
                }
            }
        }
        
        Ok(())
    }
}