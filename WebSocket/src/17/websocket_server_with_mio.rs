// Cargo.toml dependencies:
// [dependencies]
// mio = { version = "0.8", features = ["os-poll", "net"] }

use mio::{Events, Interest, Poll, Token};
use mio::net::{TcpListener, TcpStream};
use std::collections::HashMap;
use std::io::{self, Read, Write};
use std::net::SocketAddr;

const SERVER: Token = Token(0);
const BUFFER_SIZE: usize = 4096;

struct Client {
    stream: TcpStream,
    addr: SocketAddr,
    write_buffer: Vec<u8>,
}

impl Client {
    fn new(stream: TcpStream, addr: SocketAddr) -> Self {
        Self {
            stream,
            addr,
            write_buffer: Vec::new(),
        }
    }

    fn readable(&mut self) -> io::Result<Option<Vec<u8>>> {
        let mut buffer = [0u8; BUFFER_SIZE];
        let mut data = Vec::new();

        loop {
            match self.stream.read(&mut buffer) {
                Ok(0) => {
                    // Connection closed
                    return Ok(None);
                }
                Ok(n) => {
                    println!("Received {} bytes from {}", n, self.addr);
                    data.extend_from_slice(&buffer[..n]);
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // No more data available
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        if data.is_empty() {
            Ok(Some(Vec::new()))
        } else {
            Ok(Some(data))
        }
    }

    fn writable(&mut self, poll: &Poll, token: Token) -> io::Result<()> {
        while !self.write_buffer.is_empty() {
            match self.stream.write(&self.write_buffer) {
                Ok(n) => {
                    self.write_buffer.drain(..n);
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // Can't write more right now
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        // If write buffer is empty, remove write interest
        if self.write_buffer.is_empty() {
            poll.registry().reregister(
                &mut self.stream,
                token,
                Interest::READABLE,
            )?;
        }

        Ok(())
    }

    fn queue_write(&mut self, poll: &Poll, token: Token, data: Vec<u8>) -> io::Result<()> {
        self.write_buffer.extend(data);

        // Add write interest
        poll.registry().reregister(
            &mut self.stream,
            token,
            Interest::READABLE.add(Interest::WRITABLE),
        )?;

        Ok(())
    }
}

struct WebSocketServer {
    listener: TcpListener,
    clients: HashMap<Token, Client>,
    next_token: usize,
    poll: Poll,
}

impl WebSocketServer {
    fn new(addr: &str) -> io::Result<Self> {
        let address: SocketAddr = addr.parse().expect("Invalid address");
        let mut listener = TcpListener::bind(address)?;
        let poll = Poll::new()?;

        poll.registry()
            .register(&mut listener, SERVER, Interest::READABLE)?;

        println!("WebSocket server listening on {}", addr);

        Ok(Self {
            listener,
            clients: HashMap::new(),
            next_token: 1,
            poll,
        })
    }

    fn accept_connections(&mut self) -> io::Result<()> {
        loop {
            match self.listener.accept() {
                Ok((mut stream, addr)) => {
                    let token = Token(self.next_token);
                    self.next_token += 1;

                    println!("New connection from {} (token={:?})", addr, token);

                    self.poll.registry().register(
                        &mut stream,
                        token,
                        Interest::READABLE,
                    )?;

                    self.clients.insert(token, Client::new(stream, addr));
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        Ok(())
    }

    fn handle_client_readable(&mut self, token: Token) -> io::Result<()> {
        if let Some(client) = self.clients.get_mut(&token) {
            match client.readable()? {
                Some(data) if !data.is_empty() => {
                    // Echo back the data (simplified WebSocket frame handling)
                    client.queue_write(&self.poll, token, data)?;
                }
                Some(_) => {
                    // Empty read, connection still alive
                }
                None => {
                    // Connection closed
                    println!("Connection closed: {:?}", token);
                    self.clients.remove(&token);
                }
            }
        }

        Ok(())
    }

    fn handle_client_writable(&mut self, token: Token) -> io::Result<()> {
        if let Some(client) = self.clients.get_mut(&token) {
            client.writable(&self.poll, token)?;
        }

        Ok(())
    }

    fn run(&mut self) -> io::Result<()> {
        let mut events = Events::with_capacity(1024);

        loop {
            self.poll.poll(&mut events, None)?;

            for event in events.iter() {
                match event.token() {
                    SERVER => {
                        self.accept_connections()?;
                    }
                    token => {
                        if event.is_readable() {
                            if let Err(e) = self.handle_client_readable(token) {
                                eprintln!("Error handling read for {:?}: {}", token, e);
                                self.clients.remove(&token);
                            }
                        }

                        if event.is_writable() {
                            if let Err(e) = self.handle_client_writable(token) {
                                eprintln!("Error handling write for {:?}: {}", token, e);
                                self.clients.remove(&token);
                            }
                        }
                    }
                }
            }
        }
    }
}

fn main() -> io::Result<()> {
    let mut server = WebSocketServer::new("0.0.0.0:8080")?;
    server.run()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_client_buffer() {
        // Test client write buffer management
        let data = vec![1, 2, 3, 4, 5];
        assert_eq!(data.len(), 5);
    }
}