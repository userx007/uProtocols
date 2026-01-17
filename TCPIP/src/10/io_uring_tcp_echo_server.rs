// Cargo.toml dependencies:
// [dependencies]
// io-uring = "0.6"
// socket2 = "0.5"

use io_uring::{opcode, types, IoUring};
use socket2::{Domain, Protocol, Socket, Type};
use std::collections::HashMap;
use std::io;
use std::net::SocketAddr;
use std::os::unix::io::{AsRawFd, RawFd};

const QUEUE_DEPTH: u32 = 256;
const BUFFER_SIZE: usize = 4096;
const PORT: u16 = 8080;

#[derive(Debug)]
enum EventType {
    Accept,
    Read,
    Write,
}

struct ConnectionInfo {
    event_type: EventType,
    fd: RawFd,
    buffer: Vec<u8>,
}

impl ConnectionInfo {
    fn new(event_type: EventType, fd: RawFd) -> Self {
        Self {
            event_type,
            fd,
            buffer: vec![0u8; BUFFER_SIZE],
        }
    }
}

fn setup_listening_socket(port: u16) -> io::Result<Socket> {
    let socket = Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP))?;
    socket.set_reuse_address(true)?;
    
    let addr: SocketAddr = format!("0.0.0.0:{}", port).parse().unwrap();
    socket.bind(&addr.into())?;
    socket.listen(128)?;
    
    println!("Echo server listening on port {}", port);
    Ok(socket)
}

fn main() -> io::Result<()> {
    // Initialize io_uring
    let mut ring = IoUring::new(QUEUE_DEPTH)?;
    let (submitter, mut sq, mut cq) = ring.split();

    // Setup listening socket
    let listener = setup_listening_socket(PORT)?;
    let listener_fd = listener.as_raw_fd();

    // Track active operations
    let mut token_counter: u64 = 0;
    let mut buffers: HashMap<u64, Box<ConnectionInfo>> = HashMap::new();

    // Helper function to get next token
    let mut next_token = || {
        token_counter += 1;
        token_counter
    };

    // Submit initial accept operation
    let token = next_token();
    let mut conn_info = Box::new(ConnectionInfo::new(EventType::Accept, listener_fd));
    let accept_addr = conn_info.buffer.as_mut_ptr() as *mut libc::sockaddr;
    let accept_len = conn_info.buffer.as_mut_ptr().wrapping_add(64) as *mut libc::socklen_t;
    
    unsafe {
        let accept_e = opcode::Accept::new(
            types::Fd(listener_fd),
            accept_addr,
            accept_len,
        )
        .build()
        .user_data(token);
        
        sq.push(&accept_e).expect("Queue is full");
    }
    
    buffers.insert(token, conn_info);
    sq.sync();
    submitter.submit()?;

    // Event loop
    loop {
        // Wait for completion events
        cq.sync();
        
        for cqe in &mut cq {
            let token = cqe.user_data();
            let result = cqe.result();
            
            if let Some(mut info) = buffers.remove(&token) {
                if result < 0 {
                    eprintln!("Operation failed: {}", io::Error::from_raw_os_error(-result));
                    if !matches!(info.event_type, EventType::Accept) {
                        unsafe { libc::close(info.fd) };
                    }
                    continue;
                }

                match info.event_type {
                    EventType::Accept => {
                        let client_fd = result;
                        println!("Accepted new connection: fd={}", client_fd);

                        // Submit read operation for new client
                        let read_token = next_token();
                        let mut read_info = Box::new(ConnectionInfo::new(
                            EventType::Read,
                            client_fd,
                        ));
                        
                        let read_e = opcode::Recv::new(
                            types::Fd(client_fd),
                            read_info.buffer.as_mut_ptr(),
                            BUFFER_SIZE as u32,
                        )
                        .build()
                        .user_data(read_token);
                        
                        unsafe {
                            sq.push(&read_e).expect("Queue is full");
                        }
                        buffers.insert(read_token, read_info);

                        // Submit another accept operation
                        let accept_token = next_token();
                        let mut accept_info = Box::new(ConnectionInfo::new(
                            EventType::Accept,
                            listener_fd,
                        ));
                        
                        let accept_addr = accept_info.buffer.as_mut_ptr() as *mut libc::sockaddr;
                        let accept_len = accept_info.buffer.as_mut_ptr().wrapping_add(64) 
                            as *mut libc::socklen_t;
                        
                        unsafe {
                            let accept_e = opcode::Accept::new(
                                types::Fd(listener_fd),
                                accept_addr,
                                accept_len,
                            )
                            .build()
                            .user_data(accept_token);
                            
                            sq.push(&accept_e).expect("Queue is full");
                        }
                        buffers.insert(accept_token, accept_info);
                    }

                    EventType::Read => {
                        if result == 0 {
                            // Client closed connection
                            println!("Client closed connection: fd={}", info.fd);
                            unsafe { libc::close(info.fd) };
                        } else {
                            let bytes_read = result as usize;
                            println!("Received {} bytes from fd={}", bytes_read, info.fd);

                            // Submit write operation (echo back)
                            let write_token = next_token();
                            let mut write_info = Box::new(ConnectionInfo::new(
                                EventType::Write,
                                info.fd,
                            ));
                            write_info.buffer[..bytes_read]
                                .copy_from_slice(&info.buffer[..bytes_read]);

                            let write_e = opcode::Send::new(
                                types::Fd(info.fd),
                                write_info.buffer.as_ptr(),
                                bytes_read as u32,
                            )
                            .build()
                            .user_data(write_token);

                            unsafe {
                                sq.push(&write_e).expect("Queue is full");
                            }
                            buffers.insert(write_token, write_info);
                        }
                    }

                    EventType::Write => {
                        println!("Sent {} bytes to fd={}", result, info.fd);

                        // Submit another read operation
                        let read_token = next_token();
                        let mut read_info = Box::new(ConnectionInfo::new(
                            EventType::Read,
                            info.fd,
                        ));

                        let read_e = opcode::Recv::new(
                            types::Fd(info.fd),
                            read_info.buffer.as_mut_ptr(),
                            BUFFER_SIZE as u32,
                        )
                        .build()
                        .user_data(read_token);

                        unsafe {
                            sq.push(&read_e).expect("Queue is full");
                        }
                        buffers.insert(read_token, read_info);
                    }
                }
            }
        }

        // Submit all pending operations
        sq.sync();
        submitter.submit_and_wait(1)?;
    }
}

// Build: cargo build --release
// Run: cargo run
// Test: telnet localhost 8080