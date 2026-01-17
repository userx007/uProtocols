# kqueue() on BSD/macOS: Kernel Event Notification Interface

## Overview

`kqueue()` is a scalable event notification interface available on BSD-based systems (FreeBSD, OpenBSD, NetBSD) and macOS. It provides a unified mechanism for monitoring various types of events including file descriptors, signals, timers, and process state changes. Like Linux's `epoll`, kqueue is designed for high-performance I/O multiplexing but offers a more generalized event system.

## Key Concepts

### The kqueue Architecture

The kqueue system consists of two main components:

1. **kqueue**: A kernel event queue that applications create to receive event notifications
2. **kevent**: A structure that describes events to monitor and reports when those events occur

Unlike `select()` and `poll()`, kqueue uses a kernel-side state mechanism, eliminating the need to re-specify the entire interest set on each call. This makes it highly efficient for applications monitoring many file descriptors.

### Event Filters

kqueue supports multiple event filter types:

- **EVFILT_READ**: Socket/pipe/file has data available for reading
- **EVFILT_WRITE**: Socket/pipe/file is writable
- **EVFILT_AIO**: Asynchronous I/O event completion
- **EVFILT_VNODE**: File system events (file modification, deletion, etc.)
- **EVFILT_PROC**: Process events (exit, fork, exec, etc.)
- **EVFILT_SIGNAL**: Signal delivery
- **EVFILT_TIMER**: Timer expiration

This versatility makes kqueue suitable not just for network I/O, but for comprehensive system event monitoring.

## Core System Calls

### Creating a kqueue

```c
int kqueue(void);
```

Creates a new kernel event queue and returns a file descriptor. Returns -1 on error.

### Managing Events

```c
int kevent(int kq, const struct kevent *changelist, int nchanges,
           struct kevent *eventlist, int nevents,
           const struct timespec *timeout);
```

Parameters:
- `kq`: kqueue file descriptor
- `changelist`: Array of events to register/modify
- `nchanges`: Number of changes in changelist
- `eventlist`: Array to receive triggered events
- `nevents`: Maximum events to return
- `timeout`: Wait timeout (NULL for blocking)

### The kevent Structure

```c
struct kevent {
    uintptr_t ident;    // Identifier (file descriptor, signal, etc.)
    short     filter;   // Event filter type
    u_short   flags;    // Action flags
    u_int     fflags;   // Filter-specific flags
    intptr_t  data;     // Filter-specific data
    void      *udata;   // User-defined data
};
```

Common flags:
- **EV_ADD**: Add event to kqueue
- **EV_DELETE**: Remove event from kqueue
- **EV_ENABLE**: Enable event
- **EV_DISABLE**: Disable event
- **EV_ONESHOT**: Delete event after first occurrence
- **EV_CLEAR**: Reset state after retrieval

## C/C++ Implementation

### Basic TCP Server Example

```c
#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PORT 8080
#define MAX_EVENTS 64
#define BACKLOG 10

// Set socket to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd, kq;
    struct sockaddr_in server_addr;
    struct kevent change_event, events[MAX_EVENTS];
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind to address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // Set to non-blocking
    set_nonblocking(listen_fd);
    
    // Create kqueue
    kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // Register listening socket for read events
    EV_SET(&change_event, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    if (kevent(kq, &change_event, 1, NULL, 0, NULL) == -1) {
        perror("kevent register");
        close(listen_fd);
        close(kq);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d\n", PORT);
    
    // Event loop
    while (1) {
        int nev = kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);
        if (nev < 0) {
            perror("kevent wait");
            break;
        }
        
        for (int i = 0; i < nev; i++) {
            int fd = (int)events[i].ident;
            
            // New connection
            if (fd == listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("accept");
                    }
                    continue;
                }
                
                set_nonblocking(client_fd);
                
                // Register client socket for read events
                EV_SET(&change_event, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
                if (kevent(kq, &change_event, 1, NULL, 0, NULL) == -1) {
                    perror("kevent add client");
                    close(client_fd);
                    continue;
                }
                
                printf("New connection from %s:%d (fd=%d)\n",
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port),
                       client_fd);
            }
            // Data available on client socket
            else if (events[i].filter == EVFILT_READ) {
                char buffer[1024];
                ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
                
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("Received from fd=%d: %s", fd, buffer);
                    
                    // Echo back to client
                    write(fd, buffer, bytes_read);
                } else if (bytes_read == 0) {
                    // Connection closed
                    printf("Client fd=%d disconnected\n", fd);
                    EV_SET(&change_event, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                    kevent(kq, &change_event, 1, NULL, 0, NULL);
                    close(fd);
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("read");
                        close(fd);
                    }
                }
            }
        }
    }
    
    close(listen_fd);
    close(kq);
    return 0;
}
```

### Advanced Example with Timers

```c
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    int kq;
    struct kevent change_event[2], events[10];
    
    kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        exit(EXIT_FAILURE);
    }
    
    // Register timer that fires every 2 seconds
    // ident is user-defined (timer ID), data is in milliseconds
    EV_SET(&change_event[0], 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 2000, NULL);
    
    // Register one-shot timer for 5 seconds
    EV_SET(&change_event[1], 2, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 5000, NULL);
    
    if (kevent(kq, change_event, 2, NULL, 0, NULL) == -1) {
        perror("kevent register timers");
        close(kq);
        exit(EXIT_FAILURE);
    }
    
    printf("Timers registered. Waiting for events...\n");
    
    for (int count = 0; count < 5; count++) {
        int nev = kevent(kq, NULL, 0, events, 10, NULL);
        
        for (int i = 0; i < nev; i++) {
            if (events[i].filter == EVFILT_TIMER) {
                printf("Timer %lu fired (count=%ld)\n", 
                       (unsigned long)events[i].ident, 
                       (long)events[i].data);
            }
        }
    }
    
    close(kq);
    return 0;
}
```

## Rust Implementation

### Basic TCP Server with mio-style Wrapper

```rust
use std::os::unix::io::{AsRawFd, RawFd};
use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::collections::HashMap;

// Raw kqueue bindings
mod ffi {
    use libc::{c_int, c_void, intptr_t, uintptr_t};
    
    #[repr(C)]
    pub struct kevent {
        pub ident: uintptr_t,
        pub filter: i16,
        pub flags: u16,
        pub fflags: u32,
        pub data: intptr_t,
        pub udata: *mut c_void,
    }
    
    pub const EVFILT_READ: i16 = -1;
    pub const EVFILT_WRITE: i16 = -2;
    pub const EV_ADD: u16 = 0x0001;
    pub const EV_DELETE: u16 = 0x0002;
    pub const EV_ENABLE: u16 = 0x0004;
    pub const EV_ONESHOT: u16 = 0x0010;
    
    extern "C" {
        pub fn kqueue() -> c_int;
        pub fn kevent(
            kq: c_int,
            changelist: *const kevent,
            nchanges: c_int,
            eventlist: *mut kevent,
            nevents: c_int,
            timeout: *const libc::timespec,
        ) -> c_int;
    }
}

struct KQueue {
    kq: RawFd,
}

impl KQueue {
    fn new() -> io::Result<Self> {
        let kq = unsafe { ffi::kqueue() };
        if kq < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(KQueue { kq })
    }
    
    fn register(&self, fd: RawFd, filter: i16) -> io::Result<()> {
        let mut change = ffi::kevent {
            ident: fd as uintptr_t,
            filter,
            flags: ffi::EV_ADD | ffi::EV_ENABLE,
            fflags: 0,
            data: 0,
            udata: std::ptr::null_mut(),
        };
        
        let ret = unsafe {
            ffi::kevent(self.kq, &change, 1, std::ptr::null_mut(), 0, std::ptr::null())
        };
        
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
    
    fn wait(&self, events: &mut [ffi::kevent]) -> io::Result<usize> {
        let ret = unsafe {
            ffi::kevent(
                self.kq,
                std::ptr::null(),
                0,
                events.as_mut_ptr(),
                events.len() as i32,
                std::ptr::null(),
            )
        };
        
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(ret as usize)
    }
}

impl Drop for KQueue {
    fn drop(&mut self) {
        unsafe { libc::close(self.kq) };
    }
}

fn set_nonblocking(stream: &TcpStream) -> io::Result<()> {
    use std::os::unix::io::AsRawFd;
    let fd = stream.as_raw_fd();
    unsafe {
        let flags = libc::fcntl(fd, libc::F_GETFL, 0);
        if flags < 0 {
            return Err(io::Error::last_os_error());
        }
        if libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK) < 0 {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(())
}

fn main() -> io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    listener.set_nonblocking(true)?;
    
    let kq = KQueue::new()?;
    let listen_fd = listener.as_raw_fd();
    
    // Register listener for read events
    kq.register(listen_fd, ffi::EVFILT_READ)?;
    
    let mut clients: HashMap<RawFd, TcpStream> = HashMap::new();
    let mut events = vec![unsafe { std::mem::zeroed() }; 64];
    
    println!("Server listening on port 8080");
    
    loop {
        let nev = kq.wait(&mut events)?;
        
        for i in 0..nev {
            let event = &events[i];
            let fd = event.ident as RawFd;
            
            // New connection
            if fd == listen_fd {
                match listener.accept() {
                    Ok((stream, addr)) => {
                        set_nonblocking(&stream)?;
                        let client_fd = stream.as_raw_fd();
                        
                        kq.register(client_fd, ffi::EVFILT_READ)?;
                        clients.insert(client_fd, stream);
                        
                        println!("New connection from {} (fd={})", addr, client_fd);
                    }
                    Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => continue,
                    Err(e) => eprintln!("Accept error: {}", e),
                }
            }
            // Data from client
            else if event.filter == ffi::EVFILT_READ {
                if let Some(stream) = clients.get_mut(&fd) {
                    let mut buffer = [0u8; 1024];
                    
                    match stream.read(&mut buffer) {
                        Ok(0) => {
                            // Connection closed
                            println!("Client fd={} disconnected", fd);
                            clients.remove(&fd);
                        }
                        Ok(n) => {
                            print!("Received from fd={}: {}", fd, 
                                   String::from_utf8_lossy(&buffer[..n]));
                            
                            // Echo back
                            let _ = stream.write_all(&buffer[..n]);
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => continue,
                        Err(e) => {
                            eprintln!("Read error on fd={}: {}", fd, e);
                            clients.remove(&fd);
                        }
                    }
                }
            }
        }
    }
}
```

### Using the nix Crate (Higher-Level Abstraction)

```rust
use nix::sys::event::{EventFilter, EventFlag, FilterFlag, KEvent, Kqueue};
use std::os::unix::io::AsRawFd;
use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write};
use std::collections::HashMap;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    listener.set_nonblocking(true)?;
    
    let kq = Kqueue::new()?;
    let listen_fd = listener.as_raw_fd();
    
    // Create event for listening socket
    let event = KEvent::new(
        listen_fd as usize,
        EventFilter::EVFILT_READ,
        EventFlag::EV_ADD | EventFlag::EV_ENABLE,
        FilterFlag::empty(),
        0,
        0,
    );
    
    kq.kevent(&[event], &mut [], None)?;
    
    let mut clients: HashMap<i32, TcpStream> = HashMap::new();
    let mut events = vec![KEvent::new(0, EventFilter::EVFILT_READ, 
                                       EventFlag::empty(), FilterFlag::empty(), 0, 0); 64];
    
    println!("Server listening on port 8080");
    
    loop {
        let nev = kq.kevent(&[], &mut events, None)?;
        
        for event in &events[..nev] {
            let fd = event.ident() as i32;
            
            if fd == listen_fd {
                // Accept new connection
                match listener.accept() {
                    Ok((stream, addr)) => {
                        stream.set_nonblocking(true)?;
                        let client_fd = stream.as_raw_fd();
                        
                        let ev = KEvent::new(
                            client_fd as usize,
                            EventFilter::EVFILT_READ,
                            EventFlag::EV_ADD | EventFlag::EV_ENABLE,
                            FilterFlag::empty(),
                            0,
                            0,
                        );
                        kq.kevent(&[ev], &mut [], None)?;
                        
                        clients.insert(client_fd, stream);
                        println!("New connection from {} (fd={})", addr, client_fd);
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => continue,
                    Err(e) => eprintln!("Accept error: {}", e),
                }
            } else if let Some(stream) = clients.get_mut(&fd) {
                let mut buffer = [0u8; 1024];
                
                match stream.read(&mut buffer) {
                    Ok(0) => {
                        println!("Client fd={} disconnected", fd);
                        clients.remove(&fd);
                    }
                    Ok(n) => {
                        print!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
                        let _ = stream.write_all(&buffer[..n]);
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => continue,
                    Err(e) => {
                        eprintln!("Read error: {}", e);
                        clients.remove(&fd);
                    }
                }
            }
        }
    }
}
```

## Performance Characteristics

**Advantages:**
- **O(1) scalability**: Performance doesn't degrade with the number of monitored file descriptors
- **Unified interface**: Can monitor various event types (I/O, signals, timers, processes) through a single mechanism
- **Kernel-side state**: No need to repeatedly send the interest set to the kernel
- **Rich event semantics**: Filter-specific flags provide detailed event information

**Compared to other mechanisms:**
- More flexible than `epoll` due to multiple event filter types
- Better performance than `select()`/`poll()` for large numbers of file descriptors
- Native to BSD systems; `epoll` is Linux-specific

## Summary

`kqueue()` is BSD's sophisticated event notification system that excels at scalable I/O multiplexing and general system event monitoring. It uses a kernel queue where applications register interest in various event types through `kevent()` structures. The interface supports not just network I/O but also file system monitoring, process tracking, signal handling, and timers—making it a comprehensive event system.

The key advantage is its O(1) scalability and kernel-side state management, which eliminates the overhead of repeatedly specifying interest sets. In C/C++, developers work directly with the system calls and manually manage file descriptor lifecycles. In Rust, crates like `nix` provide safer abstractions while maintaining the same performance characteristics.

For high-performance network servers on BSD-based systems and macOS, kqueue is the preferred mechanism, offering both efficiency and versatility that surpasses traditional multiplexing approaches like `select()` and `poll()`.