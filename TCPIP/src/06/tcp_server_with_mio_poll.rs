// Cargo.toml dependencies:
// [dependencies]
// mio = { version = "0.8", features = ["os-poll", "net"] }

use mio::{Events, Interest, Poll, Token};
use mio::net::{TcpListener, TcpStream};
use std::collections::HashMap;
use std::io::{self, Read, Write};

const SERVER: Token = Token(0);
const BUFFER_SIZE: usize = 1024;

fn main() -> io::Result<()> {
    // Create poll instance
    let mut poll = Poll::new()?;
    let mut events = Events::with_capacity(128);
    
    // Setup listening socket
    let addr = "127.0.0.1:8080".parse().unwrap();
    let mut server = TcpListener::bind(addr)?;
    
    // Register the server socket with poll
    poll.registry()
        .register(&mut server, SERVER, Interest::READABLE)?;
    
    println!("Server listening on {}", addr);
    
    // Track client connections
    let mut clients: HashMap<Token, TcpStream> = HashMap::new();
    let mut unique_token = Token(1);
    let mut buffer = [0u8; BUFFER_SIZE];
    
    loop {
        // Wait for events
        poll.poll(&mut events, None)?;
        
        for event in events.iter() {
            match event.token() {
                SERVER => {
                    // Accept new connections
                    loop {
                        match server.accept() {
                            Ok((mut stream, address)) => {
                                println!("New connection from {}", address);
                                
                                let token = next_token(&mut unique_token);
                                
                                // Register client with poll
                                poll.registry()
                                    .register(&mut stream, token, Interest::READABLE)?;
                                
                                clients.insert(token, stream);
                            }
                            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                                // No more connections to accept
                                break;
                            }
                            Err(e) => {
                                eprintln!("Accept error: {}", e);
                                break;
                            }
                        }
                    }
                }
                token => {
                    // Handle client events
                    let done = if let Some(stream) = clients.get_mut(&token) {
                        handle_client_event(stream, event, &mut buffer)?
                    } else {
                        false
                    };
                    
                    if done {
                        // Remove disconnected client
                        if let Some(mut stream) = clients.remove(&token) {
                            poll.registry().deregister(&mut stream)?;
                            println!("Client disconnected (token={:?})", token);
                        }
                    }
                }
            }
        }
    }
}

fn next_token(current: &mut Token) -> Token {
    let next = current.0;
    current.0 += 1;
    Token(next)
}

fn handle_client_event(
    stream: &mut TcpStream,
    event: &mio::event::Event,
    buffer: &mut [u8],
) -> io::Result<bool> {
    if event.is_readable() {
        loop {
            match stream.read(buffer) {
                Ok(0) => {
                    // Connection closed
                    return Ok(true);
                }
                Ok(n) => {
                    // Echo data back
                    print!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
                    
                    // Write data back to client
                    let mut written = 0;
                    while written < n {
                        match stream.write(&buffer[written..n]) {
                            Ok(bytes) => written += bytes,
                            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                                // Socket buffer full, will continue later
                                break;
                            }
                            Err(e) => {
                                eprintln!("Write error: {}", e);
                                return Ok(true);
                            }
                        }
                    }
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // No more data available
                    break;
                }
                Err(e) => {
                    eprintln!("Read error: {}", e);
                    return Ok(true);
                }
            }
        }
    }
    
    if event.is_write_closed() || event.is_read_closed() {
        return Ok(true);
    }
    
    Ok(false)
}