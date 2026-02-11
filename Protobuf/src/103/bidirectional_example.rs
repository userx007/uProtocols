// Bidirectional TCP Communication with Protocol Buffers in Rust
// This demonstrates how the SAME sender can also be a receiver

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use prost::Message;

// Include the generated code
pub mod tutorial {
    include!(concat!(env!("OUT_DIR"), "/tutorial.rs"));
}

use tutorial::Person;

// Helper to send a protobuf message over TCP
fn send_message<T: Message>(stream: &mut TcpStream, msg: &T, label: &str) -> std::io::Result<()> {
    let mut buf = Vec::new();
    msg.encode(&mut buf).unwrap();
    
    // Send size first (4 bytes, big-endian)
    let size = (buf.len() as u32).to_be_bytes();
    stream.write_all(&size)?;
    
    // Send data
    stream.write_all(&buf)?;
    stream.flush()?;
    
    println!("[{} SENT] Message ({} bytes)", label, buf.len());
    Ok(())
}

// Helper to receive a protobuf message over TCP
fn receive_message<T: Message + Default>(stream: &mut TcpStream, label: &str) -> std::io::Result<T> {
    // Read size first (4 bytes)
    let mut size_buf = [0u8; 4];
    stream.read_exact(&mut size_buf)?;
    let size = u32::from_be_bytes(size_buf) as usize;
    
    // Read data
    let mut buf = vec![0u8; size];
    stream.read_exact(&mut buf)?;
    
    let msg = T::decode(&buf[..])
        .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))?;
    
    println!("[{} RECEIVED] Message ({} bytes)", label, size);
    Ok(msg)
}

// Server - can both SEND and RECEIVE
fn run_server() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Server listening on port 8080");
    
    let (mut stream, addr) = listener.accept()?;
    println!("Client connected from: {}", addr);
    
    // SERVER RECEIVES from client
    let received: Person = receive_message(&mut stream, "SERVER")?;
    println!("  └─> Received person: {}", received.name);
    
    // SERVER SENDS to client (response)
    let response = Person {
        name: "Server Response".to_string(),
        id: 9999,
        email: "server@example.com".to_string(),
        phones: vec![],
    };
    send_message(&mut stream, &response, "SERVER")?;
    println!("  └─> Sent response: {}", response.name);
    
    println!("\n✓ Server demonstrated bidirectional communication!");
    Ok(())
}

// Client - can both SEND and RECEIVE
fn run_client() -> std::io::Result<()> {
    std::thread::sleep(std::time::Duration::from_millis(500)); // Wait for server
    
    let mut stream = TcpStream::connect("127.0.0.1:8080")?;
    println!("Connected to server!");
    
    // CLIENT SENDS to server
    let request = Person {
        name: "Alice".to_string(),
        id: 123,
        email: "alice@example.com".to_string(),
        phones: vec![],
    };
    send_message(&mut stream, &request, "CLIENT")?;
    println!("  └─> Sent request: {}", request.name);
    
    // CLIENT RECEIVES from server (response)
    let response: Person = receive_message(&mut stream, "CLIENT")?;
    println!("  └─> Received response: {}", response.name);
    
    println!("\n✓ Client demonstrated bidirectional communication!");
    Ok(())
}

fn main() -> std::io::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        eprintln!("Usage: {} [server|client]", args[0]);
        std::process::exit(1);
    }
    
    match args[1].as_str() {
        "server" => run_server(),
        "client" => run_client(),
        _ => {
            eprintln!("Invalid mode. Use 'server' or 'client'");
            std::process::exit(1);
        }
    }
}
