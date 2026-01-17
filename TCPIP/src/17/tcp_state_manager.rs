use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream, Shutdown};
use std::time::Duration;
use std::thread;

#[derive(Debug, Clone, Copy, PartialEq)]
enum TcpState {
    Closed,
    Listen,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
}

impl TcpState {
    fn as_str(&self) -> &'static str {
        match self {
            TcpState::Closed => "CLOSED",
            TcpState::Listen => "LISTEN",
            TcpState::SynSent => "SYN_SENT",
            TcpState::SynReceived => "SYN_RECEIVED",
            TcpState::Established => "ESTABLISHED",
            TcpState::FinWait1 => "FIN_WAIT1",
            TcpState::FinWait2 => "FIN_WAIT2",
            TcpState::CloseWait => "CLOSE_WAIT",
            TcpState::Closing => "CLOSING",
            TcpState::LastAck => "LAST_ACK",
            TcpState::TimeWait => "TIME_WAIT",
        }
    }
}

/// Wrapper around TcpStream that tracks state transitions
struct TcpConnection {
    stream: TcpStream,
    current_state: TcpState,
    peer_addr: String,
}

impl TcpConnection {
    fn new(stream: TcpStream) -> io::Result<Self> {
        let peer_addr = stream.peer_addr()?.to_string();
        
        Ok(TcpConnection {
            stream,
            current_state: TcpState::Established,
            peer_addr,
        })
    }
    
    fn from_connect(addr: &str) -> io::Result<Self> {
        println!("Initiating connection to {}", addr);
        println!("State transition: CLOSED -> SYN_SENT");
        
        // TcpStream::connect handles the three-way handshake
        let stream = TcpStream::connect(addr)?;
        
        println!("State transition: SYN_SENT -> ESTABLISHED");
        
        let peer_addr = stream.peer_addr()?.to_string();
        
        Ok(TcpConnection {
            stream,
            current_state: TcpState::Established,
            peer_addr,
        })
    }
    
    fn print_state(&self, event: &str) {
        println!("[{}] Current state: {} (peer: {})", 
                 event, self.current_state.as_str(), self.peer_addr);
    }
    
    fn state_transition(&mut self, event: &str, new_state: TcpState) {
        if self.current_state != new_state {
            println!("State transition [{}]: {} -> {}", 
                     event, self.current_state.as_str(), new_state.as_str());
            self.current_state = new_state;
        }
    }
    
    fn send_data(&mut self, data: &[u8]) -> io::Result<usize> {
        if self.current_state != TcpState::Established {
            return Err(io::Error::new(
                io::ErrorKind::NotConnected,
                format!("Cannot send in {} state", self.current_state.as_str())
            ));
        }
        
        let sent = self.stream.write(data)?;
        self.stream.flush()?;
        println!("Sent {} bytes", sent);
        Ok(sent)
    }
    
    fn receive_data(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        match self.stream.read(buffer) {
            Ok(0) => {
                // Received FIN from remote
                self.state_transition("received FIN", TcpState::CloseWait);
                Ok(0)
            }
            Ok(n) => {
                println!("Received {} bytes", n);
                Ok(n)
            }
            Err(e) => Err(e),
        }
    }
    
    fn shutdown_write(&mut self) -> io::Result<()> {
        println!("\nInitiating graceful shutdown (write side)");
        
        // This sends FIN, transitioning to FIN_WAIT1
        self.state_transition("shutdown(Write)", TcpState::FinWait1);
        self.stream.shutdown(Shutdown::Write)?;
        
        // After receiving ACK for our FIN, moves to FIN_WAIT2
        thread::sleep(Duration::from_millis(100));
        self.state_transition("received ACK for FIN", TcpState::FinWait2);
        
        Ok(())
    }
    
    fn close(mut self) {
        println!("\nClosing connection");
        self.print_state("before close");
        
        // Drop causes the connection to close
        // This sends FIN if not already sent
        drop(self.stream);
        
        println!("Connection closed, entering TIME_WAIT state (2*MSL wait)");
    }
    
    /// Demonstrates half-close scenario
    fn demonstrate_half_close(&mut self) -> io::Result<()> {
        println!("\n=== Demonstrating Half-Close ===");
        
        // Send final message
        self.send_data(b"Final message before half-close\n")?;
        
        // Close write side (send FIN)
        self.shutdown_write()?;
        
        // Can still receive data
        println!("Can still receive data in half-closed state...");
        let mut buffer = [0u8; 1024];
        
        loop {
            match self.receive_data(&mut buffer) {
                Ok(0) => {
                    println!("Received FIN from remote, connection fully closed");
                    break;
                }
                Ok(n) => {
                    let data = String::from_utf8_lossy(&buffer[..n]);
                    println!("Received in half-closed state: {}", data);
                }
                Err(e) => {
                    println!("Error receiving: {}", e);
                    break;
                }
            }
        }
        
        Ok(())
    }
}

/// Server that demonstrates passive open states
struct TcpServer {
    listener: TcpListener,
    state: TcpState,
}

impl TcpServer {
    fn new(addr: &str) -> io::Result<Self> {
        println!("Creating server socket");
        println!("State: CLOSED");
        
        let listener = TcpListener::bind(addr)?;
        println!("Bound to {}", addr);
        
        // SO_REUSEADDR is set by default in Rust
        
        println!("State transition: CLOSED -> LISTEN");
        
        Ok(TcpServer {
            listener,
            state: TcpState::Listen,
        })
    }
    
    fn accept(&mut self) -> io::Result<TcpConnection> {
        println!("\nWaiting for connection (state: {})...", self.state.as_str());
        
        let (stream, addr) = self.listener.accept()?;
        
        println!("Accepted connection from {}", addr);
        println!("State transition: LISTEN -> SYN_RECEIVED -> ESTABLISHED");
        
        TcpConnection::new(stream)
    }
}

/// Client demonstration
fn run_client(server_addr: &str) -> io::Result<()> {
    println!("=== TCP Client State Demonstration ===\n");
    
    // Connect (CLOSED -> SYN_SENT -> ESTABLISHED)
    let mut conn = TcpConnection::from_connect(server_addr)?;
    conn.print_state("after connect");
    
    // Set timeouts
    conn.stream.set_read_timeout(Some(Duration::from_secs(5)))?;
    conn.stream.set_write_timeout(Some(Duration::from_secs(5)))?;
    
    // Send some data
    conn.send_data(b"Hello from Rust client\n")?;
    
    // Receive response
    let mut buffer = [0u8; 1024];
    if let Ok(n) = conn.receive_data(&mut buffer) {
        if n > 0 {
            let response = String::from_utf8_lossy(&buffer[..n]);
            println!("Received response: {}", response);
        }
    }
    
    // Demonstrate half-close
    conn.demonstrate_half_close()?;
    
    // Full close
    conn.close();
    
    println!("\nClient finished, socket in TIME_WAIT/CLOSED");
    
    Ok(())
}

/// Server demonstration
fn run_server(bind_addr: &str) -> io::Result<()> {
    println!("=== TCP Server State Demonstration ===\n");
    
    let mut server = TcpServer::new(bind_addr)?;
    
    println!("Server ready on {}", bind_addr);
    
    // Accept one connection
    let mut conn = server.accept()?;
    conn.print_state("after accept");
    
    // Receive data
    let mut buffer = [0u8; 1024];
    
    loop {
        match conn.receive_data(&mut buffer) {
            Ok(0) => {
                // Client closed (received FIN)
                println!("\nClient initiated close");
                conn.print_state("received FIN");
                
                // Server should close too to avoid staying in CLOSE_WAIT
                println!("Server closing to transition from CLOSE_WAIT");
                
                // Send any final data
                conn.send_data(b"Goodbye from server\n")?;
                
                // Close connection (sends FIN, moves to LAST_ACK)
                println!("State: CLOSE_WAIT -> LAST_ACK -> CLOSED");
                drop(conn);
                
                break;
            }
            Ok(n) => {
                let data = String::from_utf8_lossy(&buffer[..n]);
                println!("Received: {}", data);
                
                // Echo back
                conn.send_data(&buffer[..n])?;
            }
            Err(e) => {
                println!("Error: {}", e);
                break;
            }
        }
    }
    
    println!("\nServer finished");
    
    Ok(())
}

/// Demonstrates TIME_WAIT state and SO_REUSEADDR
fn demonstrate_time_wait() -> io::Result<()> {
    println!("\n=== Demonstrating TIME_WAIT State ===\n");
    
    let addr = "127.0.0.1:9999";
    
    println!("Creating first server on {}", addr);
    {
        let _server = TcpServer::new(addr)?;
        println!("Server in LISTEN state");
        // Server drops here, goes to CLOSED
    }
    
    println!("\nServer closed");
    println!("Attempting to immediately rebind to same address...");
    
    // This works because Rust sets SO_REUSEADDR by default
    match TcpServer::new(addr) {
        Ok(_) => println!("SUCCESS: Rebound immediately (SO_REUSEADDR enabled)"),
        Err(e) => println!("FAILED: {} (would need SO_REUSEADDR)", e),
    }
    
    Ok(())
}

fn main() -> io::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        println!("Usage:");
        println!("  Server: {} server [addr:port]", args[0]);
        println!("  Client: {} client [addr:port]", args[0]);
        println!("  TIME_WAIT demo: {} timewait", args[0]);
        return Ok(());
    }
    
    match args[1].as_str() {
        "server" => {
            let addr = if args.len() > 2 { &args[2] } else { "127.0.0.1:8080" };
            run_server(addr)?;
        }
        "client" => {
            let addr = if args.len() > 2 { &args[2] } else { "127.0.0.1:8080" };
            run_client(addr)?;
        }
        "timewait" => {
            demonstrate_time_wait()?;
        }
        _ => {
            println!("Unknown command: {}", args[1]);
        }
    }
    
    Ok(())
}