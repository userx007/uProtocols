use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::os::unix::io::AsRawFd;

const MAX_CLIENTS: usize = 100;
const BUFFER_SIZE: usize = 1024;

fn main() -> io::Result<()> {
    // Create and bind listening socket
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    listener.set_nonblocking(true)?;
    
    println!("Server listening on port 8080");
    
    // Initialize poll array
    let mut fds: Vec<libc::pollfd> = Vec::with_capacity(MAX_CLIENTS);
    let mut streams: Vec<Option<TcpStream>> = Vec::with_capacity(MAX_CLIENTS);
    
    // Add listening socket as first entry
    fds.push(libc::pollfd {
        fd: listener.as_raw_fd(),
        events: libc::POLLIN,
        revents: 0,
    });
    streams.push(None); // Listening socket doesn't have a stream
    
    let mut buffer = [0u8; BUFFER_SIZE];
    
    loop {
        // Call poll - wait indefinitely for events
        let ret = unsafe {
            libc::poll(
                fds.as_mut_ptr(),
                fds.len() as libc::nfds_t,
                -1, // Infinite timeout
            )
        };
        
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        
        // Check listening socket for new connections
        if fds[0].revents & libc::POLLIN != 0 {
            match listener.accept() {
                Ok((stream, addr)) => {
                    stream.set_nonblocking(true)?;
                    println!("New connection from {:?}", addr);
                    
                    // Find empty slot or add new one
                    let mut added = false;
                    for i in 1..fds.len() {
                        if fds[i].fd == -1 {
                            fds[i].fd = stream.as_raw_fd();
                            fds[i].events = libc::POLLIN;
                            fds[i].revents = 0;
                            streams[i] = Some(stream);
                            added = true;
                            break;
                        }
                    }
                    
                    if !added {
                        if fds.len() < MAX_CLIENTS {
                            fds.push(libc::pollfd {
                                fd: stream.as_raw_fd(),
                                events: libc::POLLIN,
                                revents: 0,
                            });
                            streams.push(Some(stream));
                        } else {
                            println!("Too many clients, rejecting connection");
                        }
                    }
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // No connections available right now
                }
                Err(e) => {
                    eprintln!("Accept error: {}", e);
                }
            }
            
            fds[0].revents = 0; // Clear the event
        }
        
        // Check all client sockets
        for i in 1..fds.len() {
            if fds[i].fd == -1 {
                continue;
            }
            
            let revents = fds[i].revents;
            
            // Check for data to read
            if revents & libc::POLLIN != 0 {
                if let Some(ref mut stream) = streams[i] {
                    match stream.read(&mut buffer) {
                        Ok(0) => {
                            // Connection closed
                            println!("Client disconnected (fd={})", fds[i].fd);
                            fds[i].fd = -1;
                            streams[i] = None;
                        }
                        Ok(n) => {
                            // Echo data back
                            print!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
                            if let Err(e) = stream.write_all(&buffer[..n]) {
                                eprintln!("Write error: {}", e);
                                fds[i].fd = -1;
                                streams[i] = None;
                            }
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                            // No data available
                        }
                        Err(e) => {
                            eprintln!("Read error: {}", e);
                            fds[i].fd = -1;
                            streams[i] = None;
                        }
                    }
                }
            }
            
            // Check for errors or hangup
            if revents & (libc::POLLERR | libc::POLLHUP | libc::POLLNVAL) != 0 {
                println!("Client error/hangup (fd={})", fds[i].fd);
                fds[i].fd = -1;
                streams[i] = None;
            }
            
            fds[i].revents = 0; // Clear events
        }
        
        // Compact the arrays by removing trailing inactive entries
        while fds.len() > 1 && fds.last().map_or(false, |fd| fd.fd == -1) {
            fds.pop();
            streams.pop();
        }
    }
}