# Zero-Copy Techniques in Network Programming

## Overview

Zero-copy techniques are optimization methods that eliminate or minimize the number of times data is copied between user space and kernel space during I/O operations. In traditional network I/O, data typically travels through multiple buffers: from disk to kernel buffer, kernel buffer to user space, user space back to kernel socket buffer, and finally to the network interface. Zero-copy techniques bypass these intermediate copies, significantly improving performance for high-throughput applications.

## Key Concepts

### Traditional I/O Data Flow
1. Data read from disk to kernel page cache
2. Copy from kernel space to user space buffer
3. Copy from user space back to kernel socket buffer
4. Copy from socket buffer to NIC (Network Interface Card)

### Zero-Copy Benefits
- **Reduced CPU overhead**: Fewer copy operations mean less CPU time spent moving data
- **Lower memory bandwidth usage**: Eliminates redundant memory transfers
- **Improved throughput**: More data can be transmitted per unit time
- **Reduced latency**: Fewer operations in the data path

## Main Zero-Copy Techniques

### 1. sendfile()
Transfers data directly from a file descriptor to a socket without passing through user space.

### 2. splice()
Moves data between two file descriptors through a pipe buffer, avoiding user space copies.

### 3. Memory-Mapped I/O (mmap)
Maps file contents directly into process address space, allowing direct manipulation without explicit read/write calls.

---

## C/C++ Implementation Examples

### Example 1: sendfile() - Web Server File Transfer

```c
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define BUFFER_SIZE 8192

// Traditional approach (with copying)
ssize_t traditional_send_file(int socket_fd, const char* filename) {
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return -1;
    }
    
    char buffer[BUFFER_SIZE];
    ssize_t total_sent = 0;
    ssize_t bytes_read;
    
    // Data copied from kernel to user space, then back to kernel
    while ((bytes_read = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        ssize_t sent = send(socket_fd, buffer, bytes_read, 0);
        if (sent < 0) {
            perror("send");
            close(file_fd);
            return -1;
        }
        total_sent += sent;
    }
    
    close(file_fd);
    return total_sent;
}

// Zero-copy approach using sendfile()
ssize_t zerocopy_send_file(int socket_fd, const char* filename) {
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return -1;
    }
    
    struct stat stat_buf;
    if (fstat(file_fd, &stat_buf) < 0) {
        perror("fstat");
        close(file_fd);
        return -1;
    }
    
    off_t offset = 0;
    ssize_t total_sent = 0;
    
    // Direct kernel-to-kernel transfer
    while (offset < stat_buf.st_size) {
        ssize_t sent = sendfile(socket_fd, file_fd, &offset, 
                                stat_buf.st_size - offset);
        if (sent < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            perror("sendfile");
            close(file_fd);
            return -1;
        }
        total_sent += sent;
    }
    
    close(file_fd);
    return total_sent;
}
```

### Example 2: splice() - Pipe-based Zero-Copy

```c
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>

#define SPLICE_SIZE (64 * 1024)  // 64KB chunks

// Use splice to transfer data through a pipe
ssize_t splice_file_to_socket(int socket_fd, const char* filename) {
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return -1;
    }
    
    // Create a pipe for zero-copy transfer
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        close(file_fd);
        return -1;
    }
    
    ssize_t total_transferred = 0;
    
    while (1) {
        // Splice from file to pipe (zero-copy)
        ssize_t bytes_in_pipe = splice(file_fd, NULL, pipe_fds[1], NULL,
                                       SPLICE_SIZE, SPLICE_F_MOVE);
        if (bytes_in_pipe < 0) {
            perror("splice (file->pipe)");
            break;
        }
        if (bytes_in_pipe == 0) {
            break;  // EOF
        }
        
        // Splice from pipe to socket (zero-copy)
        ssize_t bytes_sent = 0;
        while (bytes_sent < bytes_in_pipe) {
            ssize_t result = splice(pipe_fds[0], NULL, socket_fd, NULL,
                                   bytes_in_pipe - bytes_sent, SPLICE_F_MOVE);
            if (result < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                perror("splice (pipe->socket)");
                goto cleanup;
            }
            bytes_sent += result;
        }
        
        total_transferred += bytes_sent;
    }
    
cleanup:
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    close(file_fd);
    return total_transferred;
}
```

### Example 3: Memory-Mapped I/O (mmap)

```cpp
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

class MemoryMappedFile {
private:
    void* mapped_addr;
    size_t file_size;
    int fd;
    
public:
    MemoryMappedFile(const char* filename) : mapped_addr(nullptr), file_size(0), fd(-1) {
        fd = open(filename, O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error("Failed to open file");
        }
        
        struct stat sb;
        if (fstat(fd, &sb) < 0) {
            close(fd);
            throw std::runtime_error("Failed to stat file");
        }
        
        file_size = sb.st_size;
        
        // Map file into memory
        mapped_addr = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped_addr == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("mmap failed");
        }
        
        // Advise kernel about access pattern
        madvise(mapped_addr, file_size, MADV_SEQUENTIAL);
    }
    
    ~MemoryMappedFile() {
        if (mapped_addr != nullptr && mapped_addr != MAP_FAILED) {
            munmap(mapped_addr, file_size);
        }
        if (fd >= 0) {
            close(fd);
        }
    }
    
    // Send mapped memory directly to socket
    ssize_t send_to_socket(int socket_fd) {
        const char* data = static_cast<const char*>(mapped_addr);
        size_t remaining = file_size;
        ssize_t total_sent = 0;
        
        while (remaining > 0) {
            // Data is sent from mapped memory (no user-space copy)
            ssize_t sent = send(socket_fd, data + total_sent, remaining, 0);
            if (sent < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                return -1;
            }
            total_sent += sent;
            remaining -= sent;
        }
        
        return total_sent;
    }
    
    void* get_data() const { return mapped_addr; }
    size_t get_size() const { return file_size; }
};

// Example usage
int send_file_via_mmap(int socket_fd, const char* filename) {
    try {
        MemoryMappedFile mmf(filename);
        ssize_t sent = mmf.send_to_socket(socket_fd);
        std::cout << "Sent " << sent << " bytes using mmap\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return -1;
    }
}
```

---

## Rust Implementation Examples

### Example 1: sendfile() using nix crate

```rust
use nix::sys::sendfile::sendfile;
use std::fs::File;
use std::os::unix::io::AsRawFd;
use std::net::TcpStream;
use std::io::{self, Read, Write};

// Traditional approach with copying
fn traditional_send_file(mut stream: &TcpStream, filename: &str) -> io::Result<usize> {
    let mut file = File::open(filename)?;
    let mut buffer = [0u8; 8192];
    let mut total_sent = 0;
    
    loop {
        let bytes_read = file.read(&mut buffer)?;
        if bytes_read == 0 {
            break;
        }
        
        stream.write_all(&buffer[..bytes_read])?;
        total_sent += bytes_read;
    }
    
    Ok(total_sent)
}

// Zero-copy approach using sendfile
fn zerocopy_send_file(stream: &TcpStream, filename: &str) -> io::Result<usize> {
    let file = File::open(filename)?;
    let file_fd = file.as_raw_fd();
    let socket_fd = stream.as_raw_fd();
    
    let file_size = file.metadata()?.len() as usize;
    let mut offset = 0i64;
    let mut total_sent = 0;
    
    while total_sent < file_size {
        match sendfile(socket_fd, file_fd, Some(&mut offset), file_size - total_sent) {
            Ok(sent) => {
                if sent == 0 {
                    break;
                }
                total_sent += sent;
            }
            Err(e) => {
                return Err(io::Error::from_raw_os_error(e as i32));
            }
        }
    }
    
    Ok(total_sent)
}

// Example usage
fn main() -> io::Result<()> {
    use std::net::TcpListener;
    
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Server listening on port 8080");
    
    for stream in listener.incoming() {
        let stream = stream?;
        println!("Client connected");
        
        // Use zero-copy sendfile
        match zerocopy_send_file(&stream, "large_file.bin") {
            Ok(bytes) => println!("Sent {} bytes using sendfile", bytes),
            Err(e) => eprintln!("Error: {}", e),
        }
    }
    
    Ok(())
}
```

### Example 2: Memory-Mapped I/O using memmap2

```rust
use memmap2::Mmap;
use std::fs::File;
use std::io::{self, Write};
use std::net::TcpStream;

struct MemoryMappedFile {
    _file: File,
    mmap: Mmap,
}

impl MemoryMappedFile {
    fn new(filename: &str) -> io::Result<Self> {
        let file = File::open(filename)?;
        let mmap = unsafe { Mmap::map(&file)? };
        
        Ok(MemoryMappedFile {
            _file: file,
            mmap,
        })
    }
    
    fn send_to_socket(&self, stream: &mut TcpStream) -> io::Result<usize> {
        // Write mapped memory directly to socket
        stream.write_all(&self.mmap)?;
        Ok(self.mmap.len())
    }
    
    fn data(&self) -> &[u8] {
        &self.mmap
    }
}

// Example: HTTP server serving files with mmap
fn serve_file_mmap(mut stream: TcpStream, filename: &str) -> io::Result<()> {
    let mmapped_file = MemoryMappedFile::new(filename)?;
    
    // Send HTTP headers
    let response_header = format!(
        "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: application/octet-stream\r\n\r\n",
        mmapped_file.data().len()
    );
    stream.write_all(response_header.as_bytes())?;
    
    // Send file content from mapped memory
    let bytes_sent = mmapped_file.send_to_socket(&mut stream)?;
    println!("Sent {} bytes using mmap", bytes_sent);
    
    Ok(())
}
```

### Example 3: Advanced Zero-Copy with tokio and io_uring (modern async approach)

```rust
use tokio::fs::File;
use tokio::io::{self, AsyncWriteExt};
use tokio::net::TcpStream;
use std::os::unix::io::AsRawFd;

// Modern async zero-copy using tokio
async fn async_zerocopy_transfer(
    mut stream: TcpStream,
    filename: &str,
) -> io::Result<u64> {
    let file = File::open(filename).await?;
    let metadata = file.metadata().await?;
    let file_size = metadata.len();
    
    #[cfg(target_os = "linux")]
    {
        use nix::sys::sendfile::sendfile;
        use std::os::unix::io::AsRawFd;
        
        let file_fd = file.as_raw_fd();
        let socket_fd = stream.as_raw_fd();
        
        let mut offset = 0i64;
        let mut total_sent = 0u64;
        
        while total_sent < file_size {
            let remaining = (file_size - total_sent) as usize;
            
            match sendfile(socket_fd, file_fd, Some(&mut offset), remaining) {
                Ok(sent) => {
                    if sent == 0 {
                        break;
                    }
                    total_sent += sent as u64;
                }
                Err(e) => {
                    return Err(io::Error::from_raw_os_error(e as i32));
                }
            }
        }
        
        Ok(total_sent)
    }
    
    #[cfg(not(target_os = "linux"))]
    {
        // Fallback for non-Linux systems
        use tokio::io::copy;
        let mut file = file;
        copy(&mut file, &mut stream).await
    }
}

// Complete example with error handling
#[tokio::main]
async fn main() -> io::Result<()> {
    use tokio::net::TcpListener;
    
    let listener = TcpListener::bind("127.0.0.1:9000").await?;
    println!("Async server listening on port 9000");
    
    loop {
        let (stream, addr) = listener.accept().await?;
        println!("Connection from: {}", addr);
        
        tokio::spawn(async move {
            match async_zerocopy_transfer(stream, "data.bin").await {
                Ok(bytes) => println!("Transferred {} bytes", bytes),
                Err(e) => eprintln!("Transfer error: {}", e),
            }
        });
    }
}
```

---

## Performance Comparison Example (C++)

```cpp
#include <chrono>
#include <iostream>

// Benchmark helper
template<typename Func>
double benchmark(Func&& func, const char* name) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double> duration = end - start;
    std::cout << name << ": " << duration.count() << " seconds\n";
    return duration.count();
}

void run_performance_comparison(int socket_fd, const char* filename) {
    std::cout << "Performance Comparison:\n";
    std::cout << "File: " << filename << "\n\n";
    
    double traditional_time = benchmark(
        [&]() { traditional_send_file(socket_fd, filename); },
        "Traditional read/write"
    );
    
    double sendfile_time = benchmark(
        [&]() { zerocopy_send_file(socket_fd, filename); },
        "sendfile() zero-copy"
    );
    
    double mmap_time = benchmark(
        [&]() { send_file_via_mmap(socket_fd, filename); },
        "mmap zero-copy"
    );
    
    std::cout << "\nSpeedup with sendfile: " 
              << (traditional_time / sendfile_time) << "x\n";
    std::cout << "Speedup with mmap: " 
              << (traditional_time / mmap_time) << "x\n";
}
```

---

## Summary

**Zero-copy techniques** are essential optimizations for high-performance network applications, eliminating redundant data copies between kernel and user space:

- **sendfile()**: Best for simple file-to-socket transfers (web servers, file transfers); works entirely in kernel space with minimal overhead
- **splice()**: Provides flexibility for pipe-based transfers; useful for proxying and stream processing
- **mmap**: Ideal when you need to process file data in-place or make multiple accesses; trades memory for performance

**Key Benefits**: Reduced CPU usage (30-70% in high-throughput scenarios), lower memory bandwidth consumption, improved throughput, and decreased latency.

**Considerations**: Zero-copy isn't always faster for small files (overhead may exceed benefits), requires platform-specific APIs (Linux-centric), and mmap can cause page faults. Choose the technique based on your use case: sendfile() for straightforward transfers, splice() for complex pipelines, and mmap when you need data manipulation or random access.

**Modern developments**: io_uring on Linux provides even more advanced zero-copy capabilities with asynchronous I/O, representing the cutting edge of high-performance networking.