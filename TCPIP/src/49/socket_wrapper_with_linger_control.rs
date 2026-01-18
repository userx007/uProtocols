use std::net::TcpStream;
use std::os::fd::AsRawFd;
use std::time::{Duration, Instant};
use std::io::{Read, Write};

#[derive(Debug, Clone, Copy)]
pub enum LingerOption {
    /// Default behavior: close() returns immediately
    Disabled,
    /// Graceful close with timeout
    Enabled { timeout: Duration },
    /// Abortive close (RST)
    Abortive,
}

pub struct ManagedSocket {
    stream: Option<TcpStream>,
    linger: LingerOption,
}

impl ManagedSocket {
    pub fn new(stream: TcpStream, linger: LingerOption) -> std::io::Result<Self> {
        let socket = Self {
            stream: Some(stream),
            linger,
        };
        
        // Apply linger setting
        socket.apply_linger()?;
        Ok(socket)
    }
    
    fn apply_linger(&self) -> std::io::Result<()> {
        let stream = self.stream.as_ref().ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::NotConnected, "Socket closed")
        })?;
        
        let fd = stream.as_raw_fd();
        
        let linger_struct = match self.linger {
            LingerOption::Disabled => libc::linger {
                l_onoff: 0,
                l_linger: 0,
            },
            LingerOption::Enabled { timeout } => libc::linger {
                l_onoff: 1,
                l_linger: timeout.as_secs() as i32,
            },
            LingerOption::Abortive => libc::linger {
                l_onoff: 1,
                l_linger: 0,
            },
        };
        
        unsafe {
            let result = libc::setsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_LINGER,
                &linger_struct as *const _ as *const libc::c_void,
                std::mem::size_of::<libc::linger>() as libc::socklen_t,
            );
            
            if result < 0 {
                return Err(std::io::Error::last_os_error());
            }
        }
        
        Ok(())
    }
    
    pub fn send_all(&mut self, data: &[u8]) -> std::io::Result<usize> {
        let stream = self.stream.as_mut().ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::NotConnected, "Socket closed")
        })?;
        
        stream.write_all(data)?;
        stream.flush()?;
        Ok(data.len())
    }
    
    pub fn receive(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let stream = self.stream.as_mut().ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::NotConnected, "Socket closed")
        })?;
        
        stream.read(buffer)
    }
    
    pub fn close(mut self) -> Result<Duration, std::io::Error> {
        if let Some(stream) = self.stream.take() {
            println!("Closing socket with linger: {:?}", self.linger);
            
            let start = Instant::now();
            drop(stream);
            let elapsed = start.elapsed();
            
            match self.linger {
                LingerOption::Disabled => {
                    println!("Socket closed immediately (background cleanup)");
                }
                LingerOption::Enabled { timeout } => {
                    println!("Socket closed after {:?} (max: {:?})", 
                            elapsed, timeout);
                    if elapsed >= timeout {
                        println!("WARNING: Linger timeout reached!");
                    }
                }
                LingerOption::Abortive => {
                    println!("Socket aborted with RST");
                }
            }
            
            Ok(elapsed)
        } else {
            Err(std::io::Error::new(
                std::io::ErrorKind::NotConnected,
                "Socket already closed"
            ))
        }
    }
}

impl Drop for ManagedSocket {
    fn drop(&mut self) {
        if self.stream.is_some() {
            println!("WARNING: ManagedSocket dropped without explicit close!");
            println!("Consider calling close() for controlled cleanup");
        }
    }
}

// Example usage
fn example_usage() -> std::io::Result<()> {
    println!("=== ManagedSocket Examples ===\n");
    
    // Example 1: Default behavior
    println!("--- Example 1: Default Linger ---");
    {
        let stream = create_test_socket()?;
        let mut socket = ManagedSocket::new(stream, LingerOption::Disabled)?;
        socket.send_all(b"Hello with default linger")?;
        socket.close()?;
    }
    
    println!("\n--- Example 2: Graceful Linger ---");
    {
        let stream = create_test_socket()?;
        let mut socket = ManagedSocket::new(
            stream,
            LingerOption::Enabled {
                timeout: Duration::from_secs(5),
            }
        )?;
        socket.send_all(b"Hello with graceful linger")?;
        socket.close()?;
    }
    
    println!("\n--- Example 3: Abortive Close ---");
    {
        let stream = create_test_socket()?;
        let mut socket = ManagedSocket::new(stream, LingerOption::Abortive)?;
        socket.send_all(b"Hello with abortive close")?;
        socket.close()?;
    }
    
    Ok(())
}

// Helper to create a test socket
fn create_test_socket() -> std::io::Result<TcpStream> {
    use std::net::TcpListener;
    
    // Create a listener on a random port
    let listener = TcpListener::bind("127.0.0.1:0")?;
    let addr = listener.local_addr()?;
    
    // Connect to ourselves
    let handle = std::thread::spawn(move || {
        listener.accept().ok()
    });
    
    let stream = TcpStream::connect(addr)?;
    handle.join().ok();
    
    Ok(stream)
}

fn main() -> std::io::Result<()> {
    example_usage()?;
    
    println!("\n=== Best Practices ===");
    println!("✓ Use Disabled for most cases (default behavior)");
    println!("✓ Use Enabled with timeout when data delivery is critical");
    println!("✓ Use Abortive sparingly (testing, emergency situations)");
    println!("✓ Always call close() explicitly for controlled cleanup");
    println!("✓ Handle timeout errors gracefully");
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_managed_socket_creation() {
        let stream = create_test_socket().unwrap();
        let socket = ManagedSocket::new(
            stream,
            LingerOption::Enabled {
                timeout: Duration::from_secs(1),
            }
        );
        assert!(socket.is_ok());
    }
    
    #[test]
    fn test_send_receive() {
        let stream = create_test_socket().unwrap();
        let mut socket = ManagedSocket::new(stream, LingerOption::Disabled).unwrap();
        
        let data = b"Test message";
        let sent = socket.send_all(data).unwrap();
        assert_eq!(sent, data.len());
    }
    
    #[test]
    fn test_close_timing() {
        let stream = create_test_socket().unwrap();
        let socket = ManagedSocket::new(
            stream,
            LingerOption::Enabled {
                timeout: Duration::from_secs(1),
            }
        ).unwrap();
        
        let elapsed = socket.close().unwrap();
        assert!(elapsed <= Duration::from_secs(2));
    }
}