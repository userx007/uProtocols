use tokio_tungstenite::{connect_async, tungstenite::protocol::Message};
use futures_util::{SinkExt, StreamExt};
use tokio::time::{interval, Duration};
use url::Url;

#[derive(Debug, Clone, Copy, PartialEq)]
enum ConnectionState {
    Connecting,
    Open,
    Closing,
    Closed,
}

struct WebSocketConnection {
    state: ConnectionState,
    ping_count: u32,
    message_count: u32,
}

impl WebSocketConnection {
    fn new() -> Self {
        Self {
            state: ConnectionState::Connecting,
            ping_count: 0,
            message_count: 0,
        }
    }
    
    fn transition_to(&mut self, new_state: ConnectionState) {
        println!("State transition: {:?} -> {:?}", self.state, new_state);
        self.state = new_state;
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("Starting WebSocket client...");
    
    let mut connection = WebSocketConnection::new();
    
    // Establish connection
    let url = Url::parse("wss://echo.websocket.org/")?;
    println!("Connecting to: {}", url);
    
    let (ws_stream, response) = match connect_async(url).await {
        Ok((stream, resp)) => {
            println!("Handshake successful!");
            println!("Response status: {}", resp.status());
            connection.transition_to(ConnectionState::Open);
            (stream, resp)
        }
        Err(e) => {
            eprintln!("Connection failed: {}", e);
            connection.transition_to(ConnectionState::Closed);
            return Err(e.into());
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn ping task for connection maintenance
    let ping_handle = tokio::spawn(async move {
        let mut interval = interval(Duration::from_secs(30));
        let mut ping_count = 0;
        
        loop {
            interval.tick().await;
            let payload = format!("ping-{}", ping_count);
            
            match write.send(Message::Ping(payload.as_bytes().to_vec())).await {
                Ok(_) => {
                    println!("Sent ping: {}", payload);
                    ping_count += 1;
                }
                Err(e) => {
                    eprintln!("Failed to send ping: {}", e);
                    break;
                }
            }
            
            // Send a test message periodically
            if ping_count % 2 == 0 {
                let test_msg = format!("Test message #{}", ping_count / 2);
                if let Err(e) = write.send(Message::Text(test_msg.clone())).await {
                    eprintln!("Failed to send message: {}", e);
                    break;
                }
                println!("Sent message: {}", test_msg);
            }
            
            // Initiate graceful close after 5 pings
            if ping_count >= 5 {
                println!("\nInitiating graceful close...");
                let close_msg = Message::Close(Some(
                    tokio_tungstenite::tungstenite::protocol::CloseFrame {
                        code: tokio_tungstenite::tungstenite::protocol::frame::coding::CloseCode::Normal,
                        reason: "Normal closure".into(),
                    }
                ));
                
                if let Err(e) = write.send(close_msg).await {
                    eprintln!("Failed to send close frame: {}", e);
                }
                break;
            }
        }
        
        write
    });
    
    // Message receiving task
    let receive_handle = tokio::spawn(async move {
        let mut local_connection = WebSocketConnection::new();
        local_connection.transition_to(ConnectionState::Open);
        
        while let Some(message_result) = read.next().await {
            match message_result {
                Ok(message) => {
                    match message {
                        Message::Text(text) => {
                            local_connection.message_count += 1;
                            println!("Received text message #{}: {}", 
                                   local_connection.message_count, text);
                        }
                        Message::Binary(data) => {
                            println!("Received binary message: {} bytes", data.len());
                        }
                        Message::Ping(payload) => {
                            println!("Received ping: {:?}", 
                                   String::from_utf8_lossy(&payload));
                        }
                        Message::Pong(payload) => {
                            println!("Received pong: {:?}", 
                                   String::from_utf8_lossy(&payload));
                        }
                        Message::Close(frame) => {
                            if let Some(cf) = frame {
                                println!("Received close frame: code={:?}, reason={}", 
                                       cf.code, cf.reason);
                            } else {
                                println!("Received close frame without details");
                            }
                            local_connection.transition_to(ConnectionState::Closing);
                            break;
                        }
                        Message::Frame(_) => {
                            // Raw frame - typically not handled at this level
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Error receiving message: {}", e);
                    local_connection.transition_to(ConnectionState::Closed);
                    break;
                }
            }
        }
        
        println!("Message receiving loop ended");
        local_connection.transition_to(ConnectionState::Closed);
        local_connection
    });
    
    // Wait for both tasks to complete
    let mut write_back = ping_handle.await?;
    let final_connection = receive_handle.await?;
    
    // Ensure clean closure
    if final_connection.state != ConnectionState::Closed {
        println!("Forcing connection closure...");
        let _ = write_back.close().await;
    }
    
    connection.transition_to(ConnectionState::Closed);
    println!("\nConnection lifecycle completed");
    println!("Total messages exchanged: {}", final_connection.message_count);
    
    Ok(())
}

// Example of a more structured server implementation
#[cfg(test)]
mod server_example {
    use super::*;
    use tokio::net::{TcpListener, TcpStream};
    use tokio_tungstenite::accept_async;
    
    async fn handle_connection(stream: TcpStream) -> Result<(), Box<dyn std::error::Error>> {
        let addr = stream.peer_addr()?;
        println!("New connection from: {}", addr);
        
        let ws_stream = accept_async(stream).await?;
        println!("WebSocket handshake completed for: {}", addr);
        
        let (mut write, mut read) = ws_stream.split();
        
        while let Some(message_result) = read.next().await {
            match message_result {
                Ok(message) => {
                    match message {
                        Message::Text(_) | Message::Binary(_) => {
                            // Echo message back
                            write.send(message).await?;
                        }
                        Message::Ping(payload) => {
                            // Respond with pong
                            write.send(Message::Pong(payload)).await?;
                        }
                        Message::Close(frame) => {
                            println!("Client initiated close: {:?}", frame);
                            // Respond with close frame
                            write.send(Message::Close(frame)).await?;
                            break;
                        }
                        _ => {}
                    }
                }
                Err(e) => {
                    eprintln!("Error processing message from {}: {}", addr, e);
                    break;
                }
            }
        }
        
        println!("Connection closed for: {}", addr);
        Ok(())
    }
    
    pub async fn run_server() -> Result<(), Box<dyn std::error::Error>> {
        let listener = TcpListener::bind("127.0.0.1:8080").await?;
        println!("WebSocket server listening on ws://127.0.0.1:8080");
        
        while let Ok((stream, addr)) = listener.accept().await {
            tokio::spawn(async move {
                if let Err(e) = handle_connection(stream).await {
                    eprintln!("Error handling connection from {}: {}", addr, e);
                }
            });
        }
        
        Ok(())
    }
}