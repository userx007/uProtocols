use std::net::TcpStream;
use std::os::fd::AsRawFd;
use std::time::Duration;

// Import libc for direct socket option manipulation
extern crate libc;

#[derive(Debug)]
enum LingerMode {
    Default,
    Graceful(Duration),
    Abortive,
}

struct SocketLinger {
    stream: TcpStream,
}

impl SocketLinger {
    fn new(stream: TcpStream) -> Self {
        Self { stream }
    }
    
    fn set_linger(&self, mode: LingerMode) -> Result<(), std::io::Error> {
        let fd = self.stream.as_raw_fd();
        
        let linger_struct = match mode {
            LingerMode::Default => {
                println!("Setting DEFAULT linger mode:");
                println!("  - close() returns immediately");
                println!("  - TCP sends data in background");
                println!("  - Normal TIME_WAIT state");
                
                libc::linger {
                    l_onoff: 0,
                    l_linger: 0,
                }
            }
            LingerMode::Graceful(timeout) => {
                let seconds = timeout.as_secs() as i32;
                println!("Setting GRACEFUL linger mode:");
                println!("  - close() blocks up to {} seconds", seconds);
                println!("  - Waits for data to be sent and ACKed");
                println!("  - Returns error on timeout");
                
                libc::linger {
                    l_onoff: 1,
                    l_linger: seconds,
                }
            }
            LingerMode::Abortive => {
                println!("Setting ABORTIVE linger mode:");
                println!("  - Sends RST instead of FIN");
                println!("  - Discards pending data");
                println!("  - Avoids TIME_WAIT state");
                println!("  - WARNING: Can lose data!");
                
                libc::linger {
                    l_onoff: 1,
                    l_linger: 0,
                }
            }
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
    
    fn get_linger(&self) -> Result<(bool, i32), std::io::Error> {
        let fd = self.stream.as_raw_fd();
        let mut linger_struct = libc::linger {
            l_onoff: 0,
            l_linger: 0,
        };
        let mut len = std::mem::size_of::<libc::linger>() as libc::socklen_t;
        
        unsafe {
            let result = libc::getsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_LINGER,
                &mut linger_struct as *mut _ as *mut libc::c_void,
                &mut len,
            );
            
            if result < 0 {
                return Err(std::io::Error::last_os_error());
            }
        }
        
        Ok((linger_struct.l_onoff != 0, linger_struct.l_linger))
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("=== SO_LINGER Configuration Examples ===\n");
    
    // Create a socket (not connected, just for demonstration)
    let stream = TcpStream::connect("127.0.0.1:1")
        .or_else(|_| {
            // If connection fails, create a socket anyway for demonstration
            // This is just for showing the API
            use std::net::{SocketAddr, TcpListener};
            let listener = TcpListener::bind("127.0.0.1:0")?;
            let addr = listener.local_addr()?;
            
            // Create a connection to ourselves
            let handle = std::thread::spawn(move || {
                listener.accept().ok()
            });
            
            let stream = TcpStream::connect(addr)?;
            handle.join().ok();
            Ok(stream)
        })?;
    
    let socket = SocketLinger::new(stream);
    
    // Demonstrate DEFAULT mode
    println!("\n--- Configuration 1 ---");
    socket.set_linger(LingerMode::Default)?;
    let (enabled, timeout) = socket.get_linger()?;
    println!("Current: l_onoff={}, l_linger={}\n", enabled, timeout);
    
    // Demonstrate GRACEFUL mode
    println!("--- Configuration 2 ---");
    socket.set_linger(LingerMode::Graceful(Duration::from_secs(10)))?;
    let (enabled, timeout) = socket.get_linger()?;
    println!("Current: l_onoff={}, l_linger={}\n", enabled, timeout);
    
    // Demonstrate ABORTIVE mode
    println!("--- Configuration 3 ---");
    socket.set_linger(LingerMode::Abortive)?;
    let (enabled, timeout) = socket.get_linger()?;
    println!("Current: l_onoff={}, l_linger={}\n", enabled, timeout);
    
    println!("\n=== Recommendations ===");
    println!("• Use DEFAULT for most applications");
    println!("• Use GRACEFUL when data delivery is critical");
    println!("• Use ABORTIVE only in special cases (testing, emergency shutdown)");
    
    Ok(())
}