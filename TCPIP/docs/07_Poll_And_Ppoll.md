# poll() and ppoll(): Modern I/O Multiplexing

## Overview

`poll()` and `ppoll()` are system calls that provide I/O multiplexing capabilities, allowing a program to monitor multiple file descriptors to see if I/O is possible on any of them. They serve as more scalable alternatives to `select()`, overcoming several of its limitations.

## Key Differences from select()

**Advantages of poll() over select():**
- **No hard limit on file descriptors** - Unlike `select()` which is limited by `FD_SETSIZE` (typically 1024), `poll()` can handle an arbitrary number of file descriptors
- **No need to recalculate max fd** - You don't need to track the highest file descriptor number
- **Simpler API** - Uses an array of structures rather than three separate bit masks
- **More efficient for sparse sets** - Better when monitoring a small subset of a large fd range

**When select() might still be preferred:**
- When you need precise timeout down to microseconds (though `ppoll()` addresses this)
- On very old systems where `poll()` isn't available
- For compatibility with existing codebases

## How poll() Works

The `poll()` function monitors an array of file descriptors, each with specified events to watch for. It blocks until one or more file descriptors become ready for I/O or a timeout occurs.

**Function signature:**
```c
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```

**Parameters:**
- `fds` - Array of `pollfd` structures specifying file descriptors and events
- `nfds` - Number of elements in the fds array
- `timeout` - Timeout in milliseconds (-1 = infinite, 0 = return immediately)

**Return value:**
- Positive number: Number of file descriptors with events
- 0: Timeout occurred
- -1: Error (check errno)

### The pollfd Structure

```c
struct pollfd {
    int   fd;         // File descriptor
    short events;     // Requested events (input)
    short revents;    // Returned events (output)
};
```

**Common event flags:**
- `POLLIN` - Data available to read
- `POLLOUT` - Ready for writing
- `POLLERR` - Error condition
- `POLLHUP` - Hang up (connection closed)
- `POLLNVAL` - Invalid file descriptor
- `POLLPRI` - Urgent data available (out-of-band)

## ppoll(): Signal-Safe Alternative

`ppoll()` is similar to `poll()` but provides:
- **Nanosecond precision** timeout (vs millisecond for `poll()`)
- **Atomic signal mask handling** - Prevents race conditions with signals

```c
int ppoll(struct pollfd *fds, nfds_t nfds,
          const struct timespec *timeout_ts,
          const sigset_t *sigmask);
```

The signal mask is applied atomically before waiting, preventing the race condition where a signal arrives just before the poll call.

## C/C++ Implementation Examples

### Basic TCP Server using poll()

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

int main() {
    int listen_fd, new_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // Array to hold poll file descriptors
    struct pollfd fds[MAX_CLIENTS];
    int nfds = 1;  // Start with just the listening socket
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }
    
    // Bind to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    
    // Listen for connections
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Server listening on port %d\n", PORT);
    
    // Initialize poll array
    // First entry is the listening socket
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    
    // Initialize remaining entries
    for (int i = 1; i < MAX_CLIENTS; i++) {
        fds[i].fd = -1;  // -1 indicates unused entry
    }
    
    // Main event loop
    while (1) {
        // Wait for events (timeout -1 means wait indefinitely)
        int ret = poll(fds, nfds, -1);
        
        if (ret < 0) {
            perror("poll");
            break;
        }
        
        // Check if listening socket has incoming connection
        if (fds[0].revents & POLLIN) {
            new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            if (new_fd < 0) {
                perror("accept");
                continue;
            }
            
            printf("New connection from %s:%d (fd=%d)\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port),
                   new_fd);
            
            // Add new client to poll array
            int i;
            for (i = 1; i < MAX_CLIENTS; i++) {
                if (fds[i].fd < 0) {
                    fds[i].fd = new_fd;
                    fds[i].events = POLLIN;
                    break;
                }
            }
            
            if (i == MAX_CLIENTS) {
                printf("Too many clients, rejecting connection\n");
                close(new_fd);
            } else {
                if (i >= nfds) {
                    nfds = i + 1;
                }
            }
        }
        
        // Check all client sockets for data
        for (int i = 1; i < nfds; i++) {
            if (fds[i].fd < 0) {
                continue;
            }
            
            // Check for data to read
            if (fds[i].revents & POLLIN) {
                ssize_t n = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                
                if (n < 0) {
                    perror("recv");
                    close(fds[i].fd);
                    fds[i].fd = -1;
                } else if (n == 0) {
                    // Connection closed by client
                    printf("Client disconnected (fd=%d)\n", fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                } else {
                    // Echo data back to client
                    buffer[n] = '\0';
                    printf("Received from fd=%d: %s", fds[i].fd, buffer);
                    send(fds[i].fd, buffer, n, 0);
                }
            }
            
            // Check for errors or hangup
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("Client error/hangup (fd=%d)\n", fds[i].fd);
                close(fds[i].fd);
                fds[i].fd = -1;
            }
        }
        
        // Compact the array by removing inactive descriptors from the end
        while (nfds > 1 && fds[nfds - 1].fd < 0) {
            nfds--;
        }
    }
    
    // Cleanup
    for (int i = 0; i < nfds; i++) {
        if (fds[i].fd >= 0) {
            close(fds[i].fd);
        }
    }
    
    return 0;
}
```

### Using ppoll() with Signal Handling

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024

volatile sig_atomic_t should_exit = 0;

void signal_handler(int signum) {
    should_exit = 1;
}

int main() {
    int listen_fd, client_fd;
    struct sockaddr_in server_addr;
    struct pollfd fds[2];
    char buffer[BUFFER_SIZE];
    
    // Set up signal handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    
    if (listen(listen_fd, 5) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Server listening on port %d. Press Ctrl+C to exit.\n", PORT);
    
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    
    // Block SIGINT and SIGTERM during normal operation
    // They'll only be delivered during ppoll()
    sigset_t sigmask, origmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGTERM);
    sigprocmask(SIG_BLOCK, &sigmask, &origmask);
    
    // Timeout for ppoll (5 seconds)
    struct timespec timeout;
    timeout.tv_sec = 5;
    timeout.tv_nsec = 0;
    
    int nfds = 1;
    
    while (!should_exit) {
        // ppoll atomically unblocks signals during wait
        // This prevents race condition where signal arrives
        // right before we start waiting
        int ret = ppoll(fds, nfds, &timeout, &origmask);
        
        if (ret < 0) {
            if (errno == EINTR) {
                // Interrupted by signal - check should_exit
                printf("\nReceived signal, shutting down...\n");
                break;
            }
            perror("ppoll");
            break;
        }
        
        if (ret == 0) {
            // Timeout - can perform periodic tasks here
            printf("ppoll timeout - still alive\n");
            continue;
        }
        
        // Check listening socket
        if (fds[0].revents & POLLIN) {
            client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }
            
            printf("New client connected (fd=%d)\n", client_fd);
            
            if (nfds < 2) {
                fds[1].fd = client_fd;
                fds[1].events = POLLIN;
                nfds = 2;
            } else {
                printf("Already have a client, rejecting\n");
                close(client_fd);
            }
        }
        
        // Check client socket
        if (nfds > 1 && (fds[1].revents & POLLIN)) {
            ssize_t n = recv(fds[1].fd, buffer, BUFFER_SIZE - 1, 0);
            
            if (n <= 0) {
                printf("Client disconnected\n");
                close(fds[1].fd);
                nfds = 1;
            } else {
                buffer[n] = '\0';
                printf("Received: %s", buffer);
                send(fds[1].fd, buffer, n, 0);
            }
        }
        
        // Check for errors
        if (nfds > 1 && (fds[1].revents & (POLLERR | POLLHUP))) {
            printf("Client error/hangup\n");
            close(fds[1].fd);
            nfds = 1;
        }
    }
    
    // Cleanup
    if (nfds > 1) {
        close(fds[1].fd);
    }
    close(listen_fd);
    
    printf("Server shut down cleanly\n");
    return 0;
}
```

## Rust Implementation Examples

### TCP Echo Server using poll (via libc)

```rust
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
```

### Rust: Using Higher-Level Abstraction (mio crate)

```rust
// Cargo.toml dependencies:
// [dependencies]
// mio = { version = "0.8", features = ["os-poll", "net"] }

use mio::{Events, Interest, Poll, Token};
use mio::net::{TcpListener, TcpStream};
use std::collections::HashMap;
use std::io::{self, Read, Write};

const SERVER: Token = Token(0);
const BUFFER_SIZE: usize = 1024;

fn main() -> io::Result<()> {
    // Create poll instance
    let mut poll = Poll::new()?;
    let mut events = Events::with_capacity(128);
    
    // Setup listening socket
    let addr = "127.0.0.1:8080".parse().unwrap();
    let mut server = TcpListener::bind(addr)?;
    
    // Register the server socket with poll
    poll.registry()
        .register(&mut server, SERVER, Interest::READABLE)?;
    
    println!("Server listening on {}", addr);
    
    // Track client connections
    let mut clients: HashMap<Token, TcpStream> = HashMap::new();
    let mut unique_token = Token(1);
    let mut buffer = [0u8; BUFFER_SIZE];
    
    loop {
        // Wait for events
        poll.poll(&mut events, None)?;
        
        for event in events.iter() {
            match event.token() {
                SERVER => {
                    // Accept new connections
                    loop {
                        match server.accept() {
                            Ok((mut stream, address)) => {
                                println!("New connection from {}", address);
                                
                                let token = next_token(&mut unique_token);
                                
                                // Register client with poll
                                poll.registry()
                                    .register(&mut stream, token, Interest::READABLE)?;
                                
                                clients.insert(token, stream);
                            }
                            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                                // No more connections to accept
                                break;
                            }
                            Err(e) => {
                                eprintln!("Accept error: {}", e);
                                break;
                            }
                        }
                    }
                }
                token => {
                    // Handle client events
                    let done = if let Some(stream) = clients.get_mut(&token) {
                        handle_client_event(stream, event, &mut buffer)?
                    } else {
                        false
                    };
                    
                    if done {
                        // Remove disconnected client
                        if let Some(mut stream) = clients.remove(&token) {
                            poll.registry().deregister(&mut stream)?;
                            println!("Client disconnected (token={:?})", token);
                        }
                    }
                }
            }
        }
    }
}

fn next_token(current: &mut Token) -> Token {
    let next = current.0;
    current.0 += 1;
    Token(next)
}

fn handle_client_event(
    stream: &mut TcpStream,
    event: &mio::event::Event,
    buffer: &mut [u8],
) -> io::Result<bool> {
    if event.is_readable() {
        loop {
            match stream.read(buffer) {
                Ok(0) => {
                    // Connection closed
                    return Ok(true);
                }
                Ok(n) => {
                    // Echo data back
                    print!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
                    
                    // Write data back to client
                    let mut written = 0;
                    while written < n {
                        match stream.write(&buffer[written..n]) {
                            Ok(bytes) => written += bytes,
                            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                                // Socket buffer full, will continue later
                                break;
                            }
                            Err(e) => {
                                eprintln!("Write error: {}", e);
                                return Ok(true);
                            }
                        }
                    }
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // No more data available
                    break;
                }
                Err(e) => {
                    eprintln!("Read error: {}", e);
                    return Ok(true);
                }
            }
        }
    }
    
    if event.is_write_closed() || event.is_read_closed() {
        return Ok(true);
    }
    
    Ok(false)
}
```

## Advanced Usage Patterns

### Handling Write Readiness

Sometimes you need to monitor when a socket becomes writable, especially when dealing with non-blocking I/O and large amounts of data:

```c
// When you have data to write but socket buffer is full
fds[i].events = POLLIN | POLLOUT;  // Monitor both read and write

// In the event loop
if (fds[i].revents & POLLOUT) {
    // Socket is ready for writing
    ssize_t sent = send(fds[i].fd, pending_data, pending_len, 0);
    if (sent > 0) {
        // Update pending data
        pending_data += sent;
        pending_len -= sent;
        
        if (pending_len == 0) {
            // All data sent, stop monitoring POLLOUT
            fds[i].events = POLLIN;
        }
    }
}
```

### Edge-Triggered vs Level-Triggered

Unlike `epoll()` which supports edge-triggered mode, `poll()` is inherently level-triggered:

- **Level-triggered**: Event is reported as long as the condition is true
  - If data is available for reading, `POLLIN` will be reported on every poll call until the data is read
  - More forgiving but can lead to redundant notifications

### Timeout Strategies

```c
// Different timeout patterns

// 1. No timeout - wait indefinitely
poll(fds, nfds, -1);

// 2. Immediate return - just check status
poll(fds, nfds, 0);

// 3. Specific timeout - 5 seconds
poll(fds, nfds, 5000);

// 4. Periodic tasks with timeout
while (running) {
    int ret = poll(fds, nfds, 1000);  // 1 second
    if (ret == 0) {
        // Timeout - perform periodic maintenance
        cleanup_old_connections();
        log_statistics();
    }
}
```

### Error Handling Best Practices

```c
int ret = poll(fds, nfds, timeout);

if (ret < 0) {
    if (errno == EINTR) {
        // Interrupted by signal - usually safe to retry
        continue;
    } else if (errno == ENOMEM) {
        // Out of memory - serious error
        fprintf(stderr, "poll: out of memory\n");
        break;
    } else {
        perror("poll");
        break;
    }
}

// Always check revents flags
if (fds[i].revents & POLLNVAL) {
    // Invalid file descriptor - probably already closed
    fprintf(stderr, "Invalid fd: %d\n", fds[i].fd);
    fds[i].fd = -1;
}

if (fds[i].revents & POLLERR) {
    // Error on socket - check with getsockopt
    int error;
    socklen_t len = sizeof(error);
    getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &error, &len);
    fprintf(stderr, "Socket error: %s\n", strerror(error));
}
```

## Performance Considerations

### Complexity

- **poll()**: O(n) where n is the number of file descriptors
  - Must scan entire array to find ready descriptors
  - Must reset revents on each call (kernel does this)

- **Comparison with alternatives**:
  - `select()`: Also O(n), but limited by FD_SETSIZE
  - `epoll()`: O(1) for most operations (better for large numbers of fds)

### When to Use poll()

**Good use cases:**
- Moderate number of file descriptors (< 1000)
- File descriptors that change frequently
- Simple applications where code clarity is important
- Cross-platform code (more portable than epoll)

**Consider alternatives when:**
- Handling thousands of connections (`epoll()` or `kqueue()`)
- Need edge-triggered semantics (`epoll()`)
- Building high-performance servers (consider `io_uring`)

### Optimization Tips

1. **Keep nfds minimal**: Remove inactive entries to reduce scanning
2. **Compact the array**: Move inactive entries to the end and reduce nfds
3. **Avoid unnecessary events**: Don't monitor POLLOUT unless you need to write
4. **Batch operations**: Process multiple ready descriptors before calling poll again

## Common Pitfalls

1. **Not checking revents properly**
   ```c
   // Wrong - only checks POLLIN
   if (fds[i].revents == POLLIN)
   
   // Correct - uses bitwise AND
   if (fds[i].revents & POLLIN)
   ```

2. **Forgetting to close file descriptors**
   ```c
   // Must close before removing from array
   close(fds[i].fd);
   fds[i].fd = -1;
   ```

3. **Not handling POLLHUP correctly**
   - POLLHUP can occur with POLLIN (data + hangup)
   - Read remaining data before closing

4. **Reusing revents values**
   ```c
   // Wrong - revents is output only
   fds[i].revents = POLLIN;
   
   // Correct - set events, kernel sets revents
   fds[i].events = POLLIN;
   ```

## Summary

`poll()` and `ppoll()` provide robust I/O multiplexing with several advantages over `select()`:

**Key Strengths:**
- No arbitrary limit on file descriptor numbers
- Simpler API with struct-based interface
- Better scalability for moderate loads
- `ppoll()` adds nanosecond precision and atomic signal handling

**When to Choose:**
- Use `poll()` for simple to moderate complexity servers (dozens to hundreds of connections)
- Use `ppoll()` when you need precise timeouts or signal-safe operation
- Consider `epoll()` (Linux) or `kqueue()` (BSD) for high-performance scenarios with thousands of connections
- Modern async runtimes (Tokio, async-std) abstract these details in Rust

**Best Practices:**
- Always check revents with bitwise AND operators
- Handle POLLHUP and POLLERR appropriately
- Close file descriptors before marking them as inactive
- Keep the poll array compact for better performance
- Use appropriate timeouts for your application's needs

The examples provided demonstrate practical implementations in both C/C++ and Rust, showing both low-level system call usage and higher-level abstractions that make poll-based I/O more ergonomic and safe.