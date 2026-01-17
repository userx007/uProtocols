# io_uring: Next-Generation Asynchronous I/O Interface for Linux

## Detailed Description

**io_uring** is a modern, high-performance asynchronous I/O interface introduced in Linux kernel 5.1 (2019). It was designed by Jens Axboe to address the limitations of existing I/O mechanisms like `select()`, `poll()`, `epoll()`, and Linux AIO (`libaio`). The name "io_uring" comes from its use of ring buffers (circular queues) for communication between user space and kernel space.

### Core Concepts

**1. Ring Buffer Architecture**

io_uring uses two primary ring buffers shared between user space and kernel:

- **Submission Queue (SQ)**: Where applications place I/O requests
- **Completion Queue (CQ)**: Where the kernel places I/O completion events

This design minimizes system calls and enables true zero-copy I/O operations.

**2. Submission Queue Entries (SQE)**

Each I/O operation is described by an SQE structure containing:
- Operation type (read, write, accept, connect, etc.)
- File descriptor
- Buffer address and length
- Offset for file operations
- Flags and user data for correlation

**3. Completion Queue Entries (CQE)**

When operations complete, the kernel writes CQEs containing:
- Result code (bytes transferred or error)
- User data (copied from SQE for request/response correlation)
- Flags indicating completion status

### Key Advantages

**Performance Benefits:**
- **Reduced System Calls**: Batch multiple operations in a single system call
- **Zero-Copy**: Direct kernel-userspace memory sharing
- **Kernel Polling**: Can eliminate interrupts entirely for ultra-low latency
- **Efficient Batching**: Submit and reap multiple operations at once

**Versatility:**
- Supports network I/O, file I/O, and various other operations
- Works with regular files, block devices, sockets, pipes
- Supports advanced operations: vectored I/O, direct I/O, fixed buffers

**Modern Features:**
- **Registered Buffers**: Pre-register memory regions for even faster I/O
- **Registered Files**: Pre-register file descriptors to avoid lookup overhead
- **Linked Operations**: Chain operations with dependencies
- **Task Work**: Run cleanup tasks efficiently

### Use Cases

- High-performance network servers (web servers, proxies, databases)
- Storage systems requiring maximum I/O throughput
- Real-time applications needing predictable latency
- Applications handling thousands of concurrent connections

---

## Code Examples

### C/C++ Example: Basic TCP Echo Server with io_uring

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>

#define QUEUE_DEPTH 256
#define BUFFER_SIZE 4096
#define PORT 8080

enum {
    EVENT_ACCEPT,
    EVENT_READ,
    EVENT_WRITE
};

typedef struct {
    int event_type;
    int fd;
    char *buffer;
    size_t len;
} conn_info;

conn_info* create_conn_info(int event_type, int fd) {
    conn_info *info = malloc(sizeof(conn_info));
    info->event_type = event_type;
    info->fd = fd;
    info->buffer = malloc(BUFFER_SIZE);
    info->len = 0;
    return info;
}

void free_conn_info(conn_info *info) {
    if (info->buffer) free(info->buffer);
    free(info);
}

int setup_listening_socket(int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    int enable = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(port);

    if (bind(sock_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind");
        return -1;
    }

    if (listen(sock_fd, 128) < 0) {
        perror("listen");
        return -1;
    }

    return sock_fd;
}

void add_accept_request(struct io_uring *ring, int server_fd, 
                       struct sockaddr_in *client_addr, socklen_t *client_len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    conn_info *info = create_conn_info(EVENT_ACCEPT, server_fd);
    
    io_uring_prep_accept(sqe, server_fd, (struct sockaddr*)client_addr, 
                        client_len, 0);
    io_uring_sqe_set_data(sqe, info);
}

void add_read_request(struct io_uring *ring, int client_fd) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    conn_info *info = create_conn_info(EVENT_READ, client_fd);
    
    io_uring_prep_recv(sqe, client_fd, info->buffer, BUFFER_SIZE, 0);
    io_uring_sqe_set_data(sqe, info);
}

void add_write_request(struct io_uring *ring, int client_fd, 
                      char *buffer, size_t len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    conn_info *info = create_conn_info(EVENT_WRITE, client_fd);
    
    memcpy(info->buffer, buffer, len);
    info->len = len;
    
    io_uring_prep_send(sqe, client_fd, info->buffer, len, 0);
    io_uring_sqe_set_data(sqe, info);
}

int main() {
    struct io_uring ring;
    struct io_uring_cqe *cqe;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Initialize io_uring
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return 1;
    }

    // Setup listening socket
    int server_fd = setup_listening_socket(PORT);
    if (server_fd < 0) {
        return 1;
    }

    printf("Echo server listening on port %d\n", PORT);

    // Add initial accept request
    add_accept_request(&ring, server_fd, &client_addr, &client_len);
    io_uring_submit(&ring);

    // Event loop
    while (1) {
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            perror("io_uring_wait_cqe");
            break;
        }

        conn_info *info = (conn_info*)io_uring_cqe_get_data(cqe);
        int result = cqe->res;

        if (result < 0) {
            fprintf(stderr, "Operation failed: %s\n", strerror(-result));
            if (info->event_type != EVENT_ACCEPT) {
                close(info->fd);
            }
            free_conn_info(info);
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        switch (info->event_type) {
            case EVENT_ACCEPT: {
                int client_fd = result;
                printf("Accepted new connection: fd=%d\n", client_fd);
                
                // Add read request for new client
                add_read_request(&ring, client_fd);
                
                // Add another accept request
                add_accept_request(&ring, server_fd, &client_addr, &client_len);
                io_uring_submit(&ring);
                break;
            }

            case EVENT_READ: {
                if (result == 0) {
                    // Client closed connection
                    printf("Client closed connection: fd=%d\n", info->fd);
                    close(info->fd);
                } else {
                    printf("Received %d bytes from fd=%d\n", result, info->fd);
                    
                    // Echo back the data
                    add_write_request(&ring, info->fd, info->buffer, result);
                    io_uring_submit(&ring);
                }
                break;
            }

            case EVENT_WRITE: {
                printf("Sent %d bytes to fd=%d\n", result, info->fd);
                
                // Continue reading from this client
                add_read_request(&ring, info->fd);
                io_uring_submit(&ring);
                break;
            }
        }

        free_conn_info(info);
        io_uring_cqe_seen(&ring, cqe);
    }

    close(server_fd);
    io_uring_queue_exit(&ring);
    return 0;
}

// Compile: gcc -o echo_server echo_server.c -luring
// Run: ./echo_server
// Test: telnet localhost 8080
```

### Rust Example: Asynchronous TCP Server with io_uring

```rust
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
```

### Advanced C++ Example: File I/O with Registered Buffers---

```cpp
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <liburing.h>
#include <vector>

class IoUringFileReader {
private:
    struct io_uring ring;
    std::vector<iovec> iovecs;
    int queue_depth;

public:
    IoUringFileReader(int depth = 32) : queue_depth(depth) {
        if (io_uring_queue_init(queue_depth, &ring, 0) < 0) {
            throw std::runtime_error("Failed to initialize io_uring");
        }
    }

    ~IoUringFileReader() {
        // Unregister buffers if registered
        if (!iovecs.empty()) {
            io_uring_unregister_buffers(&ring);
        }
        io_uring_queue_exit(&ring);
    }

    // Register fixed buffers for zero-copy I/O
    bool register_buffers(size_t buffer_count, size_t buffer_size) {
        iovecs.resize(buffer_count);
        
        for (size_t i = 0; i < buffer_count; i++) {
            void* buffer = aligned_alloc(4096, buffer_size);
            if (!buffer) {
                std::cerr << "Failed to allocate buffer" << std::endl;
                return false;
            }
            iovecs[i].iov_base = buffer;
            iovecs[i].iov_len = buffer_size;
        }

        if (io_uring_register_buffers(&ring, iovecs.data(), iovecs.size()) < 0) {
            std::cerr << "Failed to register buffers" << std::endl;
            return false;
        }

        std::cout << "Registered " << buffer_count << " buffers of " 
                  << buffer_size << " bytes each" << std::endl;
        return true;
    }

    // Read file using registered buffers
    ssize_t read_file_async(const char* filename) {
        int fd = open(filename, O_RDONLY);
        if (fd < 0) {
            perror("open");
            return -1;
        }

        // Get file size
        off_t file_size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        std::cout << "Reading file: " << filename 
                  << " (size: " << file_size << " bytes)" << std::endl;

        ssize_t total_read = 0;
        size_t buffer_size = iovecs[0].iov_len;
        int outstanding_reads = 0;
        off_t offset = 0;

        // Submit read requests
        while (offset < file_size || outstanding_reads > 0) {
            // Submit new reads up to queue depth
            while (offset < file_size && outstanding_reads < queue_depth) {
                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                if (!sqe) {
                    break;
                }

                int buf_index = outstanding_reads % iovecs.size();
                size_t read_size = std::min(buffer_size, 
                                           static_cast<size_t>(file_size - offset));

                // Use fixed buffer read for better performance
                io_uring_prep_read_fixed(sqe, fd, 
                                        iovecs[buf_index].iov_base,
                                        read_size, offset, buf_index);
                
                io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(offset));

                offset += read_size;
                outstanding_reads++;
            }

            // Submit all queued operations
            int submitted = io_uring_submit(&ring);
            if (submitted < 0) {
                std::cerr << "io_uring_submit failed" << std::endl;
                break;
            }

            // Wait for completions
            struct io_uring_cqe* cqe;
            int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                std::cerr << "io_uring_wait_cqe failed" << std::endl;
                break;
            }

            // Process completion
            off_t cqe_offset = reinterpret_cast<off_t>(
                io_uring_cqe_get_data(cqe));
            int bytes_read = cqe->res;

            if (bytes_read < 0) {
                std::cerr << "Read error at offset " << cqe_offset 
                         << ": " << strerror(-bytes_read) << std::endl;
            } else {
                total_read += bytes_read;
                std::cout << "Read " << bytes_read << " bytes at offset " 
                         << cqe_offset << std::endl;
            }

            io_uring_cqe_seen(&ring, cqe);
            outstanding_reads--;
        }

        close(fd);
        return total_read;
    }

    // Copy file using io_uring (demonstrates both read and write)
    bool copy_file(const char* src, const char* dst) {
        int src_fd = open(src, O_RDONLY);
        int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (src_fd < 0 || dst_fd < 0) {
            perror("open");
            if (src_fd >= 0) close(src_fd);
            if (dst_fd >= 0) close(dst_fd);
            return false;
        }

        off_t file_size = lseek(src_fd, 0, SEEK_END);
        lseek(src_fd, 0, SEEK_SET);

        std::cout << "Copying " << file_size << " bytes from " 
                  << src << " to " << dst << std::endl;

        const size_t chunk_size = 1024 * 1024; // 1MB chunks
        char* buffer = static_cast<char*>(aligned_alloc(4096, chunk_size));
        off_t offset = 0;

        while (offset < file_size) {
            size_t to_copy = std::min(chunk_size, 
                                     static_cast<size_t>(file_size - offset));

            // Read operation
            struct io_uring_sqe* sqe_r = io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe_r, src_fd, buffer, to_copy, offset);
            io_uring_sqe_set_data(sqe_r, reinterpret_cast<void*>(1)); // READ marker

            io_uring_submit(&ring);

            // Wait for read to complete
            struct io_uring_cqe* cqe_r;
            io_uring_wait_cqe(&ring, &cqe_r);
            
            int bytes_read = cqe_r->res;
            io_uring_cqe_seen(&ring, cqe_r);

            if (bytes_read < 0) {
                std::cerr << "Read error: " << strerror(-bytes_read) << std::endl;
                break;
            }

            // Write operation
            struct io_uring_sqe* sqe_w = io_uring_get_sqe(&ring);
            io_uring_prep_write(sqe_w, dst_fd, buffer, bytes_read, offset);
            io_uring_sqe_set_data(sqe_w, reinterpret_cast<void*>(2)); // WRITE marker

            io_uring_submit(&ring);

            // Wait for write to complete
            struct io_uring_cqe* cqe_w;
            io_uring_wait_cqe(&ring, &cqe_w);
            
            int bytes_written = cqe_w->res;
            io_uring_cqe_seen(&ring, cqe_w);

            if (bytes_written < 0) {
                std::cerr << "Write error: " << strerror(-bytes_written) << std::endl;
                break;
            }

            offset += bytes_written;
            std::cout << "Progress: " << (offset * 100 / file_size) << "%\r" << std::flush;
        }

        std::cout << std::endl << "Copy completed" << std::endl;

        free(buffer);
        close(src_fd);
        close(dst_fd);
        return true;
    }
};

int main(int argc, char* argv[]) {
    try {
        IoUringFileReader reader(64);

        // Example 1: Register buffers and read a file
        if (reader.register_buffers(8, 4096)) {
            if (argc > 1) {
                reader.read_file_async(argv[1]);
            }
        }

        // Example 2: Copy a file
        if (argc > 2) {
            reader.copy_file(argv[1], argv[2]);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// Compile: g++ -std=c++17 -o file_reader file_reader.cpp -luring
// Usage: ./file_reader input.txt [output.txt]
```

## Summary

**io_uring** represents a paradigm shift in Linux I/O, offering:

**Key Technical Features:**
- **Dual ring buffers** (submission/completion queues) for efficient kernel-userspace communication
- **Minimal system calls** through batching and shared memory
- **Comprehensive operation support**: network I/O, file I/O, filesystem operations, timers, and more
- **Advanced optimizations**: registered buffers/files, kernel polling, linked operations

**Performance Characteristics:**
- Dramatically reduces context switches (up to 90% fewer system calls)
- Achieves near-zero-copy I/O with registered buffers
- Scales efficiently to hundreds of thousands of operations per second
- Provides predictable, low-latency performance for real-time applications

**Practical Advantages:**
- Unifies asynchronous I/O model across different I/O types (network, file, etc.)
- Simplifies application architecture compared to epoll + thread pools
- Enables new optimization patterns impossible with traditional APIs
- Future-proof design with ongoing kernel development

**Ecosystem:**
- Native Linux kernel support (5.1+)
- Libraries: `liburing` (C), `io-uring` crate (Rust), `tokio-uring` (async Rust)
- Growing adoption in high-performance servers (databases, web servers, proxies)

io_uring is particularly valuable for applications requiring maximum I/O throughput, minimal latency, or handling massive concurrency. It's becoming the standard for high-performance network and storage applications on Linux, gradually replacing older interfaces like epoll and Linux AIO.