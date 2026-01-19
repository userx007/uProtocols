use std::collections::VecDeque;
use std::error::Error;
use std::fmt;
use std::io::{self, ErrorKind, Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

// Custom error types for WebSocket operations
#[derive(Debug, Clone)]
pub enum WebSocketError {
    BrokenPipe,
    ConnectionReset,
    Timeout,
    NetworkUnreachable,
    DnsFailure(String),
    ProtocolError(String),
    IoError(String),
}

impl fmt::Display for WebSocketError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            WebSocketError::BrokenPipe => write!(f, "Broken pipe - remote closed connection"),
            WebSocketError::ConnectionReset => write!(f, "Connection reset by peer"),
            WebSocketError::Timeout => write!(f, "Connection timeout"),
            WebSocketError::NetworkUnreachable => write!(f, "Network unreachable"),
            WebSocketError::DnsFailure(msg) => write!(f, "DNS failure: {}", msg),
            WebSocketError::ProtocolError(msg) => write!(f, "Protocol error: {}", msg),
            WebSocketError::IoError(msg) => write!(f, "I/O error: {}", msg),
        }
    }
}

impl Error for WebSocketError {}

impl WebSocketError {
    pub fn is_recoverable(&self) -> bool {
        matches!(
            self,
            WebSocketError::BrokenPipe
                | WebSocketError::ConnectionReset
                | WebSocketError::Timeout
                | WebSocketError::NetworkUnreachable
        )
    }

    pub fn from_io_error(e: &io::Error) -> Self {
        match e.kind() {
            ErrorKind::BrokenPipe => WebSocketError::BrokenPipe,
            ErrorKind::ConnectionReset => WebSocketError::ConnectionReset,
            ErrorKind::TimedOut => WebSocketError::Timeout,
            ErrorKind::NetworkUnreachable => WebSocketError::NetworkUnreachable,
            _ => WebSocketError::IoError(e.to_string()),
        }
    }
}

// Connection policy trait for different retry strategies
pub trait ConnectionPolicy: Send {
    fn next_backoff(&mut self, retry_count: u32) -> Duration;
    fn should_retry(&self, retry_count: u32) -> bool;
    fn reset(&mut self);
}

// Exponential backoff policy
pub struct ExponentialBackoff {
    initial: Duration,
    max: Duration,
    max_retries: u32,
    current: Duration,
}

impl ExponentialBackoff {
    pub fn new(initial: Duration, max: Duration, max_retries: u32) -> Self {
        Self {
            initial,
            max,
            max_retries,
            current: initial,
        }
    }
}

impl ConnectionPolicy for ExponentialBackoff {
    fn next_backoff(&mut self, retry_count: u32) -> Duration {
        if retry_count == 0 {
            self.current = self.initial;
        } else {
            self.current = std::cmp::min(self.current * 2, self.max);
        }
        self.current
    }

    fn should_retry(&self, retry_count: u32) -> bool {
        retry_count < self.max_retries
    }

    fn reset(&mut self) {
        self.current = self.initial;
    }
}

// Message buffer for queuing during disconnection
#[derive(Clone)]
struct BufferedMessage {
    data: Vec<u8>,
    timestamp: Instant,
}

pub struct MessageBuffer {
    buffer: VecDeque<BufferedMessage>,
    max_size: usize,
}

impl MessageBuffer {
    pub fn new(max_size: usize) -> Self {
        Self {
            buffer: VecDeque::with_capacity(max_size),
            max_size,
        }
    }

    pub fn push(&mut self, data: Vec<u8>) -> bool {
        if self.buffer.len() >= self.max_size {
            return false;
        }
        self.buffer.push_back(BufferedMessage {
            data,
            timestamp: Instant::now(),
        });
        true
    }

    pub fn pop(&mut self) -> Option<BufferedMessage> {
        self.buffer.pop_front()
    }

    pub fn len(&self) -> usize {
        self.buffer.len()
    }

    pub fn clear(&mut self) {
        self.buffer.clear();
    }
}

// Connection state
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error,
    Reconnecting,
}

// WebSocket connection with error recovery
pub struct WebSocketConnection {
    host: String,
    port: u16,
    stream: Option<TcpStream>,
    state: ConnectionState,
    retry_count: u32,
    policy: Box<dyn ConnectionPolicy>,
    message_buffer: Arc<Mutex<MessageBuffer>>,
    last_activity: Instant,
    error_callback: Option<Box<dyn Fn(&WebSocketError) + Send + Sync>>,
    state_callback: Option<Box<dyn Fn(ConnectionState, ConnectionState) + Send + Sync>>,
}

impl WebSocketConnection {
    pub fn new(
        host: String,
        port: u16,
        policy: Box<dyn ConnectionPolicy>,
    ) -> Self {
        Self {
            host,
            port,
            stream: None,
            state: ConnectionState::Disconnected,
            retry_count: 0,
            policy,
            message_buffer: Arc::new(Mutex::new(MessageBuffer::new(1000))),
            last_activity: Instant::now(),
            error_callback: None,
            state_callback: None,
        }
    }

    pub fn set_error_callback<F>(&mut self, callback: F)
    where
        F: Fn(&WebSocketError) + Send + Sync + 'static,
    {
        self.error_callback = Some(Box::new(callback));
    }

    pub fn set_state_callback<F>(&mut self, callback: F)
    where
        F: Fn(ConnectionState, ConnectionState) + Send + Sync + 'static,
    {
        self.state_callback = Some(Box::new(callback));
    }

    fn change_state(&mut self, new_state: ConnectionState) {
        let old_state = self.state;
        self.state = new_state;
        if let Some(ref callback) = self.state_callback {
            callback(old_state, new_state);
        }
    }

    fn handle_error(&mut self, error: WebSocketError) {
        eprintln!("Error: {}", error);
        
        if let Some(ref callback) = self.error_callback {
            callback(&error);
        }

        if error.is_recoverable() {
            self.change_state(ConnectionState::Error);
        }
    }

    pub fn connect(&mut self) -> Result<(), WebSocketError> {
        self.change_state(ConnectionState::Connecting);

        let addr = format!("{}:{}", self.host, self.port);
        let socket_addrs = addr.to_socket_addrs()
            .map_err(|e| WebSocketError::DnsFailure(e.to_string()))?;

        let mut last_err = None;
        for addr in socket_addrs {
            match TcpStream::connect_timeout(&addr, Duration::from_secs(5)) {
                Ok(mut stream) => {
                    // Set socket options
                    stream.set_read_timeout(Some(Duration::from_secs(5)))
                        .map_err(|e| WebSocketError::IoError(e.to_string()))?;
                    stream.set_write_timeout(Some(Duration::from_secs(5)))
                        .map_err(|e| WebSocketError::IoError(e.to_string()))?;
                    stream.set_nodelay(true)
                        .map_err(|e| WebSocketError::IoError(e.to_string()))?;

                    self.stream = Some(stream);
                    self.change_state(ConnectionState::Connected);
                    self.retry_count = 0;
                    self.policy.reset();
                    self.last_activity = Instant::now();

                    self.flush_message_buffer()?;
                    println!("Connected to {}:{}", self.host, self.port);
                    return Ok(());
                }
                Err(e) => {
                    eprintln!("Failed to connect to {}: {}", addr, e);
                    last_err = Some(e);
                }
            }
        }

        let err = WebSocketError::from_io_error(
            last_err.as_ref().unwrap()
        );
        self.handle_error(err.clone());
        Err(err)
    }

    pub fn send(&mut self, data: &[u8]) -> Result<usize, WebSocketError> {
        if self.state != ConnectionState::Connected {
            // Buffer the message
            let mut buffer = self.message_buffer.lock().unwrap();
            if buffer.push(data.to_vec()) {
                println!("Message buffered (state: {:?})", self.state);
            } else {
                eprintln!("Message buffer full, dropping message");
            }
            return Err(WebSocketError::ProtocolError("Not connected".to_string()));
        }

        let stream = self.stream.as_mut().ok_or_else(|| {
            WebSocketError::ProtocolError("No stream available".to_string())
        })?;

        match stream.write(data) {
            Ok(n) => {
                self.last_activity = Instant::now();
                Ok(n)
            }
            Err(e) => {
                let error = WebSocketError::from_io_error(&e);
                
                // Buffer the message for retry
                if error.is_recoverable() {
                    let mut buffer = self.message_buffer.lock().unwrap();
                    buffer.push(data.to_vec());
                }
                
                self.handle_error(error.clone());
                Err(error)
            }
        }
    }

    pub fn receive(&mut self, buffer: &mut [u8]) -> Result<usize, WebSocketError> {
        if self.state != ConnectionState::Connected {
            return Err(WebSocketError::ProtocolError("Not connected".to_string()));
        }

        let stream = self.stream.as_mut().ok_or_else(|| {
            WebSocketError::ProtocolError("No stream available".to_string())
        })?;

        match stream.read(buffer) {
            Ok(0) => {
                println!("Connection closed by peer");
                self.change_state(ConnectionState::Disconnected);
                Err(WebSocketError::ConnectionReset)
            }
            Ok(n) => {
                self.last_activity = Instant::now();
                Ok(n)
            }
            Err(e) if e.kind() == ErrorKind::WouldBlock || e.kind() == ErrorKind::TimedOut => {
                Ok(0) // Non-fatal
            }
            Err(e) => {
                let error = WebSocketError::from_io_error(&e);
                self.handle_error(error.clone());
                Err(error)
            }
        }
    }

    pub fn reconnect(&mut self) -> Result<(), WebSocketError> {
        if !self.policy.should_retry(self.retry_count) {
            eprintln!("Max retries exceeded");
            return Err(WebSocketError::ProtocolError("Max retries exceeded".to_string()));
        }

        self.change_state(ConnectionState::Reconnecting);

        let backoff = self.policy.next_backoff(self.retry_count);
        println!(
            "Reconnecting in {:?} (attempt {})...",
            backoff,
            self.retry_count + 1
        );

        thread::sleep(backoff);

        // Close old connection
        self.stream = None;

        self.retry_count += 1;

        self.connect()
    }

    pub fn disconnect(&mut self) {
        self.stream = None;
        self.change_state(ConnectionState::Disconnected);
        println!("Disconnected");
    }

    fn flush_message_buffer(&mut self) -> Result<(), WebSocketError> {
        let mut buffer = self.message_buffer.lock().unwrap();
        while let Some(msg) = buffer.pop() {
            println!("Flushing buffered message ({} bytes)", msg.data.len());
            if let Some(ref mut stream) = self.stream {
                stream.write_all(&msg.data)
                    .map_err(|e| WebSocketError::from_io_error(&e))?;
            }
        }
        Ok(())
    }

    pub fn check_connection_health(&mut self) {
        if self.state != ConnectionState::Connected {
            return;
        }

        let elapsed = self.last_activity.elapsed();
        if elapsed > Duration::from_secs(60) {
            println!("Connection idle for {:?}, sending heartbeat", elapsed);
            let _ = self.send(b"PING");
        }
    }

    pub fn get_state(&self) -> ConnectionState {
        self.state
    }

    pub fn buffered_messages(&self) -> usize {
        self.message_buffer.lock().unwrap().len()
    }
}

// Example usage
fn main() -> Result<(), Box<dyn Error>> {
    let policy = Box::new(ExponentialBackoff::new(
        Duration::from_secs(1),
        Duration::from_secs(32),
        10,
    ));

    let mut conn = WebSocketConnection::new(
        "echo.websocket.org".to_string(),
        80,
        policy,
    );

    conn.set_error_callback(|err| {
        eprintln!("Error callback: {} (recoverable: {})", err, err.is_recoverable());
    });

    conn.set_state_callback(|old_state, new_state| {
        println!("State changed: {:?} -> {:?}", old_state, new_state);
    });

    // Initial connection
    if conn.connect().is_err() {
        // Attempt reconnection
        while conn.reconnect().is_err() {
            if conn.get_state() == ConnectionState::Disconnected {
                break;
            }
        }
    }

    // Main loop with error recovery
    for i in 0..100 {
        match conn.get_state() {
            ConnectionState::Error | ConnectionState::Disconnected => {
                let _ = conn.reconnect();
                continue;
            }
            ConnectionState::Connected => {
                conn.check_connection_health();

                let msg = format!("Message {}", i);
                if let Err(e) = conn.send(msg.as_bytes()) {
                    eprintln!("Send failed: {}", e);
                }

                // Simulate receiving
                let mut buffer = [0u8; 4096];
                match conn.receive(&mut buffer) {
                    Ok(n) if n > 0 => {
                        println!("Received {} bytes", n);
                    }
                    Ok(_) => {}
                    Err(e) => eprintln!("Receive failed: {}", e),
                }

                thread::sleep(Duration::from_secs(1));
            }
            _ => {
                thread::sleep(Duration::from_millis(100));
            }
        }
    }

    conn.disconnect();
    Ok(())
}