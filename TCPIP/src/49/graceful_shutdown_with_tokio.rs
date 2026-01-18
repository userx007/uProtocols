// Cargo.toml dependencies:
// tokio = { version = "1", features = ["full"] }
// libc = "0.2"

use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::os::fd::AsRawFd;
use std::time::{Duration, Instant};

/// Sets SO_LINGER option on a TcpStream
fn set_linger(stream: &TcpStream, timeout_secs: Option<u32>) -> std::io::Result<()> {
    let fd = stream.as_raw_fd();
    
    let linger = match timeout_secs {
        Some(secs) => libc::linger {
            l_onoff: 1,
            l_linger: secs as i32,
        },
        None => libc::linger {
            l_onoff: 0,
            l_linger: 0,
        },
    };
    
    unsafe {
        let result = libc::setsockopt(
            fd,
            libc::SOL_SOCKET,
            libc::SO_LINGER,
            &linger as *const _ as *const libc::c_void,
            std::mem::size_of::<libc::linger>() as libc::socklen_t,
        );
        
        if result < 0 {
            return Err(std::io::Error::last_os_error());
        }
    }
    
    Ok(())
}

/// Gracefully close a connection with proper cleanup
async fn graceful_close(
    mut stream: TcpStream,
    context: &str
) -> std::io::Result<()> {
    println!("[{}] Initiating graceful shutdown...", context);
    
    // Set SO_LINGER before closing
    const LINGER_TIMEOUT: u32 = 5;
    set_linger(&stream, Some(LINGER_TIMEOUT))?;
    
    // Shutdown the write half (send FIN)
    stream.shutdown().await?;
    println!("[{}] Sent FIN, waiting for peer acknowledgment...", context);
    
    // Read until EOF (peer closes their write side)
    let mut buffer = [0u8; 1024];
    loop {
        match stream.read(&mut buffer).await {
            Ok(0) => {
                println!("[{}] Received EOF from peer", context);
                break;
            }
            Ok(n) => {
                println!("[{}] Draining {} bytes from peer", context, n);
            }
            Err(e) => {
                println!("[{}] Error reading: {}", context, e);
                break;
            }
        }
    }
    
    // Now close the socket (with linger)
    let start = Instant::now();
    drop(stream);
    let elapsed = start.elapsed();
    
    println!("[{}] Socket closed after {:?}", context, elapsed);
    println!("[{}] Graceful shutdown complete", context);
    
    Ok(())
}

/// Handle a client connection with guaranteed data delivery
async fn handle_client(stream: TcpStream, addr: std::net::SocketAddr) {
    println!("New connection from: {}", addr);
    
    let mut stream = stream;
    let mut buffer = [0u8; 1024];
    
    // Read request
    match stream.read(&mut buffer).await {
        Ok(n) if n > 0 => {
            let request = String::from_utf8_lossy(&buffer[..n]);
            println!("Received: {}", request.trim());
            
            // Prepare response
            let response = format!(
                "HTTP/1.1 200 OK\r\n\
                 Content-Type: text/plain\r\n\
                 Content-Length: 30\r\n\
                 Connection: close\r\n\
                 \r\n\
                 Graceful shutdown successful!\n"
            );
            
            // Send response with error handling
            match stream.write_all(response.as_bytes()).await {
                Ok(_) => {
                    println!("Response sent successfully");
                    
                    // Flush to ensure data is sent
                    if let Err(e) = stream.flush().await {
                        eprintln!("Flush error: {}", e);
                    }
                }
                Err(e) => {
                    eprintln!("Send error: {}", e);
                }
            }
        }
        Ok(_) => println!("Client closed connection immediately"),
        Err(e) => eprintln!("Read error: {}", e),
    }
    
    // Perform graceful close
    if let Err(e) = graceful_close(stream, &format!("Client {}", addr)).await {
        eprintln!("Error during graceful close: {}", e);
    }
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("Server listening on 127.0.0.1:8080");
    println!("Using SO_LINGER for guaranteed data delivery\n");
    
    // Accept connections
    loop {
        match listener.accept().await {
            Ok((stream, addr)) => {
                // Spawn a task for each connection
                tokio::spawn(async move {
                    handle_client(stream, addr).await;
                });
            }
            Err(e) => {
                eprintln!("Accept error: {}", e);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[tokio::test]
    async fn test_graceful_shutdown() {
        // Start a test server
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        
        // Spawn server task
        tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            graceful_close(stream, "Test").await.ok();
        });
        
        // Connect client
        let mut client = TcpStream::connect(addr).await.unwrap();
        
        // Send some data
        client.write_all(b"Test data").await.unwrap();
        
        // Read response (should be EOF after graceful close)
        let mut buffer = [0u8; 1024];
        let n = client.read(&mut buffer).await.unwrap();
        assert_eq!(n, 0, "Should receive EOF");
    }
}