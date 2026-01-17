# Connection Leak Detection

## Overview

Connection leak detection is a critical debugging and monitoring technique for identifying file descriptors and sockets that are opened but never properly closed. These leaks can accumulate over time, eventually exhausting system resources and causing application failures. Unlike memory leaks which consume RAM, connection leaks consume limited operating system resources like file descriptors, leading to "too many open files" errors.

## Understanding the Problem

Every process has a limit on the number of file descriptors it can open simultaneously (typically 1024 on many Unix systems, though configurable). When sockets or files aren't properly closed due to:

- Exception paths not being handled
- Early returns in functions
- Forgotten cleanup in error conditions
- Reference counting bugs
- Lost object references

These file descriptors remain open until the process terminates, gradually depleting the available pool.

## Detection Strategies

### 1. **Monitoring Open File Descriptors**
Track the number of open file descriptors over time to detect upward trends.

### 2. **Resource Tracking**
Maintain explicit tracking of opened/closed resources in debug builds.

### 3. **Static Analysis**
Use tools to verify that every open/socket call has a corresponding close.

### 4. **Testing Under Load**
Run long-duration tests to expose leaks that only appear after many operations.

---

## C/C++ Implementation

### Basic Detection Using /proc Filesystem (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>

// Count open file descriptors for current process
int count_open_fds() {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/fd", getpid());
    
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return -1;
    }
    
    int count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") != 0 && 
            strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// Example usage
int main() {
    printf("Initial FD count: %d\n", count_open_fds());
    
    // Simulate leak
    FILE *leak = fopen("/dev/null", "r");
    printf("After fopen (leaked): %d\n", count_open_fds());
    
    // Proper cleanup
    FILE *proper = fopen("/dev/null", "r");
    printf("After second fopen: %d\n", count_open_fds());
    fclose(proper);
    printf("After fclose: %d\n", count_open_fds());
    
    return 0;
}
```

### Advanced Socket Leak Tracker (C++)

```cpp
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <execinfo.h>
#include <cstring>

class SocketLeakDetector {
private:
    struct SocketInfo {
        std::string location;
        void* backtrace[10];
        int backtrace_size;
        long long timestamp;
    };
    
    std::unordered_map<int, SocketInfo> tracked_sockets;
    std::mutex mutex;
    bool enabled;

public:
    SocketLeakDetector() : enabled(true) {}
    
    // Track socket creation
    void track_socket(int fd, const char* file, int line) {
        if (!enabled) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        SocketInfo info;
        info.location = std::string(file) + ":" + std::to_string(line);
        info.backtrace_size = backtrace(info.backtrace, 10);
        info.timestamp = time(nullptr);
        
        tracked_sockets[fd] = info;
        std::cout << "[TRACK] Socket " << fd << " opened at " 
                  << info.location << std::endl;
    }
    
    // Track socket closure
    void untrack_socket(int fd, const char* file, int line) {
        if (!enabled) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        auto it = tracked_sockets.find(fd);
        
        if (it != tracked_sockets.end()) {
            std::cout << "[TRACK] Socket " << fd << " closed at " 
                      << file << ":" << line << std::endl;
            tracked_sockets.erase(it);
        } else {
            std::cerr << "[ERROR] Attempt to close untracked socket " 
                      << fd << " at " << file << ":" << line << std::endl;
        }
    }
    
    // Report leaks
    void report_leaks() {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (tracked_sockets.empty()) {
            std::cout << "[LEAK REPORT] No leaks detected!" << std::endl;
            return;
        }
        
        std::cout << "[LEAK REPORT] Found " << tracked_sockets.size() 
                  << " leaked socket(s):" << std::endl;
        
        for (const auto& pair : tracked_sockets) {
            std::cout << "  Socket FD " << pair.first 
                      << " opened at " << pair.second.location
                      << " (age: " << (time(nullptr) - pair.second.timestamp) 
                      << "s)" << std::endl;
            
            // Print backtrace
            char** symbols = backtrace_symbols(pair.second.backtrace, 
                                               pair.second.backtrace_size);
            if (symbols) {
                for (int i = 0; i < pair.second.backtrace_size; i++) {
                    std::cout << "    " << symbols[i] << std::endl;
                }
                free(symbols);
            }
        }
    }
    
    size_t get_open_count() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex));
        return tracked_sockets.size();
    }
};

// Global detector instance
SocketLeakDetector g_detector;

// Wrapper macros for tracking
#define TRACKED_SOCKET(domain, type, protocol) \
    ({ int fd = socket(domain, type, protocol); \
       if (fd >= 0) g_detector.track_socket(fd, __FILE__, __LINE__); \
       fd; })

#define TRACKED_CLOSE(fd) \
    ({ g_detector.untrack_socket(fd, __FILE__, __LINE__); \
       close(fd); })

// Example usage
int main() {
    std::cout << "=== Connection Leak Detection Demo ===" << std::endl;
    
    // Create some sockets
    int sock1 = TRACKED_SOCKET(AF_INET, SOCK_STREAM, 0);
    int sock2 = TRACKED_SOCKET(AF_INET, SOCK_STREAM, 0);
    int sock3 = TRACKED_SOCKET(AF_INET, SOCK_DGRAM, 0);
    
    std::cout << "\nOpen sockets: " << g_detector.get_open_count() << std::endl;
    
    // Close some properly
    TRACKED_CLOSE(sock1);
    TRACKED_CLOSE(sock3);
    
    std::cout << "\nAfter closing 2 sockets: " 
              << g_detector.get_open_count() << std::endl;
    
    // sock2 is leaked (not closed)
    
    // Report at end
    std::cout << "\n=== Final Leak Report ===" << std::endl;
    g_detector.report_leaks();
    
    return 0;
}
```

---

## Rust Implementation

Rust's ownership system prevents most connection leaks automatically, but they can still occur with:
- Manual file descriptor management via FFI
- Reference counting cycles with `Rc<RefCell<>>`
- Explicit `std::mem::forget` calls
- Long-lived objects holding sockets

### File Descriptor Tracking in Rust

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use std::os::unix::io::RawFd;

#[derive(Debug, Clone)]
struct FdInfo {
    location: String,
    timestamp: u64,
}

struct FdLeakDetector {
    tracked: Arc<Mutex<HashMap<RawFd, FdInfo>>>,
}

impl FdLeakDetector {
    fn new() -> Self {
        FdLeakDetector {
            tracked: Arc::new(Mutex::new(HashMap::new())),
        }
    }
    
    fn track(&self, fd: RawFd, file: &str, line: u32) {
        let mut tracked = self.tracked.lock().unwrap();
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        let info = FdInfo {
            location: format!("{}:{}", file, line),
            timestamp,
        };
        
        tracked.insert(fd, info);
        println!("[TRACK] FD {} opened at {}", fd, info.location);
    }
    
    fn untrack(&self, fd: RawFd, file: &str, line: u32) {
        let mut tracked = self.tracked.lock().unwrap();
        
        if tracked.remove(&fd).is_some() {
            println!("[TRACK] FD {} closed at {}:{}", fd, file, line);
        } else {
            eprintln!("[ERROR] Closing untracked FD {} at {}:{}", 
                     fd, file, line);
        }
    }
    
    fn report_leaks(&self) {
        let tracked = self.tracked.lock().unwrap();
        
        if tracked.is_empty() {
            println!("[LEAK REPORT] No leaks detected!");
            return;
        }
        
        println!("[LEAK REPORT] Found {} leaked FD(s):", tracked.len());
        
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        for (fd, info) in tracked.iter() {
            let age = now - info.timestamp;
            println!("  FD {} opened at {} (age: {}s)", 
                    fd, info.location, age);
        }
    }
    
    fn get_open_count(&self) -> usize {
        self.tracked.lock().unwrap().len()
    }
}

// Macros for convenience
macro_rules! track_fd {
    ($detector:expr, $fd:expr) => {
        $detector.track($fd, file!(), line!())
    };
}

macro_rules! untrack_fd {
    ($detector:expr, $fd:expr) => {
        $detector.untrack($fd, file!(), line!())
    };
}

// Example with standard library
use std::net::TcpListener;
use std::os::unix::io::AsRawFd;

fn main() {
    println!("=== Rust Connection Leak Detection Demo ===\n");
    
    let detector = FdLeakDetector::new();
    
    // Create listeners
    let listener1 = TcpListener::bind("127.0.0.1:0").unwrap();
    let fd1 = listener1.as_raw_fd();
    track_fd!(detector, fd1);
    
    let listener2 = TcpListener::bind("127.0.0.1:0").unwrap();
    let fd2 = listener2.as_raw_fd();
    track_fd!(detector, fd2);
    
    println!("\nOpen FDs: {}", detector.get_open_count());
    
    // Close one properly (drop does this automatically in Rust)
    untrack_fd!(detector, fd1);
    drop(listener1);
    
    println!("After closing 1 FD: {}", detector.get_open_count());
    
    // listener2 would normally be cleaned up by Drop,
    // but we can forget it to simulate a leak
    std::mem::forget(listener2);
    
    println!("\n=== Final Leak Report ===");
    detector.report_leaks();
}
```

### RAII-based Socket Wrapper with Automatic Leak Detection

```rust
use std::os::unix::io::{AsRawFd, RawFd};
use std::sync::Arc;
use std::net::TcpStream;

struct TrackedSocket {
    inner: TcpStream,
    detector: Arc<FdLeakDetector>,
    location: String,
}

impl TrackedSocket {
    fn new(stream: TcpStream, detector: Arc<FdLeakDetector>, 
           file: &str, line: u32) -> Self {
        let fd = stream.as_raw_fd();
        let location = format!("{}:{}", file, line);
        detector.track(fd, file, line);
        
        TrackedSocket {
            inner: stream,
            detector,
            location,
        }
    }
    
    fn get_ref(&self) -> &TcpStream {
        &self.inner
    }
}

impl Drop for TrackedSocket {
    fn drop(&mut self) {
        let fd = self.inner.as_raw_fd();
        // Extract file and line from stored location
        let parts: Vec<&str> = self.location.split(':').collect();
        if parts.len() == 2 {
            self.detector.untrack(fd, parts[0], 
                                 parts[1].parse().unwrap_or(0));
        }
    }
}

impl AsRawFd for TrackedSocket {
    fn as_raw_fd(&self) -> RawFd {
        self.inner.as_raw_fd()
    }
}

// Usage example
fn example_with_wrapper() {
    let detector = Arc::new(FdLeakDetector::new());
    
    {
        let _sock = TrackedSocket::new(
            TcpStream::connect("127.0.0.1:80").unwrap(),
            detector.clone(),
            file!(),
            line!()
        );
        
        // Socket automatically tracked and untracked via RAII
    } // _sock dropped here, automatically untracked
    
    detector.report_leaks();
}
```

---

## Summary

**Connection leak detection** is essential for building robust network applications. Key takeaways:

- **C/C++ requires manual tracking** since there's no automatic resource management. Use RAII wrappers, smart pointers, or explicit tracking systems.

- **Rust's ownership model prevents most leaks** through automatic Drop implementation, but manual FD management and `std::mem::forget` can still cause issues.

- **Detection strategies include**:
  - Monitoring `/proc/<pid>/fd` on Linux
  - Wrapping socket/close calls with tracking logic
  - Using RAII patterns to ensure cleanup
  - Collecting backtraces for leak origin identification
  - Periodic reporting and alerting

- **Production systems should**:
  - Monitor FD usage via metrics
  - Set appropriate ulimits
  - Implement graceful degradation when approaching limits
  - Log warnings for long-lived connections
  - Use testing frameworks that verify resource cleanup

- **Common leak sources**:
  - Exception handling paths
  - Early returns without cleanup
  - Reference counting cycles
  - Forgotten error handling branches
  - Asynchronous operations that never complete

By implementing systematic leak detection during development and monitoring in production, you can identify and fix resource leaks before they cause system failures.