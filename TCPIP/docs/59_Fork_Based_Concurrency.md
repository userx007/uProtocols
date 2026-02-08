# Fork-based Concurrency in TCP/IP Programming

## What's Inside:

**Core Concepts:**
- Fork-per-connection model and how `fork()` creates child processes
- Pre-fork server architectures (static, dynamic, Apache-style)
- Process management strategies including zombie prevention, graceful shutdown, and load balancing

**Code Examples:**

**C/C++** implementations:
- Basic fork-per-connection echo server with SIGCHLD handling
- Pre-fork server with static worker pool using file locks
- Dynamic pre-fork server with shared memory for adaptive worker scaling

**Rust** implementations:
- Fork-per-connection server using the `nix` crate
- Pre-fork server with serialized accept using file locking
- Modern process pool manager with dynamic worker spawning

**Key Technical Details:**
- Zombie process prevention with signal handlers
- Accept serialization to prevent thundering herd
- Shared memory for inter-process communication
- Graceful shutdown and hot restart patterns
- Performance trade-offs vs threads and async/event-driven models

**Summary:**
Fork-based concurrency provides strong process isolation and stability at the cost of higher memory overhead and limited scalability. While modern servers often use event-driven or async models, fork-based approaches remain valuable for CPU-intensive workloads, security-critical applications, and situations requiring simple, robust isolation between connections.

## Table of Contents
1. [Introduction](#introduction)
2. [Understanding Fork-based Concurrency](#understanding-fork-based-concurrency)
3. [Pre-fork Server Models](#pre-fork-server-models)
4. [Process Management Strategies](#process-management-strategies)
5. [C/C++ Implementation Examples](#cc-implementation-examples)
6. [Rust Implementation Examples](#rust-implementation-examples)
7. [Performance Considerations](#performance-considerations)
8. [Summary](#summary)

---

## Introduction

Fork-based concurrency is a fundamental approach to building concurrent servers on Unix-like systems. It leverages the `fork()` system call to create child processes that handle client connections independently. This model was one of the earliest and most straightforward methods for achieving concurrency in network servers.

### Key Concepts
- **Process-based concurrency**: Each connection handled by a separate process
- **Process isolation**: Memory space separation provides security and stability
- **Pre-forking**: Creating worker processes before connections arrive
- **Process pooling**: Maintaining a pool of ready-to-serve worker processes

---

## Understanding Fork-based Concurrency

### The Fork System Call

The `fork()` system call creates a new process by duplicating the calling process. The new process (child) is an exact copy of the parent process, including:
- Memory contents (copy-on-write)
- File descriptors
- Signal handlers
- Current working directory

After forking:
- **Parent process**: Receives the child's PID (> 0)
- **Child process**: Receives 0
- **Error**: Returns -1

### Fork-per-Connection Model

In the traditional fork-per-connection model:
1. Server accepts a connection
2. Forks a child process
3. Child handles the client communication
4. Parent continues accepting new connections
5. Child exits when done

**Advantages:**
- Simple to implement
- Strong isolation between connections
- Crash in one connection doesn't affect others
- Can leverage multiple CPU cores

**Disadvantages:**
- High overhead for fork() operation
- Context switching costs
- Resource intensive (each process has its own memory space)
- Limited scalability (OS process limits)

---

## Pre-fork Server Models

Pre-forking addresses the overhead of creating a new process for each connection by creating a pool of worker processes in advance.

### Architecture

```
                    [Master Process]
                           |
        +------------------+------------------+
        |                  |                  |
   [Worker 1]         [Worker 2]         [Worker N]
        |                  |                  |
   accept()           accept()           accept()
        |                  |                  |
   [Client 1]         [Client 2]         [Client N]
```

### Pre-fork Strategies

#### 1. **Static Pre-fork**
- Fixed number of worker processes
- Created at startup
- Remain alive for server lifetime
- Simple but inflexible

#### 2. **Dynamic Pre-fork**
- Adjusts worker count based on load
- Monitors idle/busy workers
- Spawns new workers when needed
- Terminates excess idle workers
- Used by Apache MPM (Multi-Processing Module)

#### 3. **Serialized Accept**
- Workers compete for incoming connections
- Prevents "thundering herd" problem
- Uses locking mechanism (file lock, semaphore)
- Only one worker accepts at a time

#### 4. **Socket Passing**
- Master accepts connections
- Passes socket to worker via Unix domain socket
- More complex but offers better control
- Can implement load balancing

---

## Process Management Strategies

### 1. Zombie Process Prevention

When a child process terminates, it becomes a zombie until the parent calls `wait()` or `waitpid()`. Strategies:

- **Signal Handlers**: Install SIGCHLD handler to reap children
- **Non-blocking wait**: Use `waitpid(-1, NULL, WNOHANG)` in SIGCHLD handler
- **Double Fork**: Fork twice to orphan the grandchild (init inherits it)

### 2. Graceful Shutdown

- **Master sends SIGTERM** to all workers
- Workers finish current requests
- Timeout for forced termination (SIGKILL)
- Close listening socket to prevent new connections

### 3. Resource Limits

- Set `RLIMIT_NPROC` (max processes per user)
- Set `RLIMIT_NOFILE` (max open files)
- Monitor and limit memory usage per process
- Implement connection timeouts

### 4. Load Balancing

- **Round-robin**: OS kernel distributes accepts naturally
- **Least connections**: Track active connections per worker
- **Custom algorithms**: Based on request type, worker capacity

### 5. Hot Restart

- Fork new workers with updated code
- New connections go to new workers
- Old workers finish existing connections
- Seamless upgrade without downtime

---

## C/C++ Implementation Examples

### Example 1: Basic Fork-per-Connection Server

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define PORT 8080
#define BACKLOG 10

// SIGCHLD handler to prevent zombie processes
void sigchld_handler(int sig) {
    (void)sig;
    // Reap all terminated children
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_client(int client_fd) {
    char buffer[1024];
    ssize_t n;
    
    // Simple echo server
    while ((n = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("Received: %s", buffer);
        
        // Echo back to client
        if (write(client_fd, buffer, n) != n) {
            perror("write error");
            break;
        }
        
        // Break if client sends "quit"
        if (strncmp(buffer, "quit", 4) == 0) {
            break;
        }
    }
    
    if (n < 0) {
        perror("read error");
    }
}

int main() {
    int listen_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    pid_t pid;
    
    // Install SIGCHLD handler
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    // Create socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    
    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }
    
    // Bind
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    
    // Listen
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Server listening on port %d\n", PORT);
    
    // Main accept loop
    while (1) {
        client_len = sizeof(client_addr);
        client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            perror("accept");
            continue;
        }
        
        // Fork child process
        pid = fork();
        
        if (pid < 0) {
            perror("fork");
            close(client_fd);
            continue;
        }
        
        if (pid == 0) {
            // Child process
            close(listen_fd); // Child doesn't need listening socket
            handle_client(client_fd);
            close(client_fd);
            exit(0); // Child exits after handling client
        } else {
            // Parent process
            close(client_fd); // Parent doesn't need client socket
        }
    }
    
    close(listen_fd);
    return 0;
}
```

### Example 2: Pre-fork Server with Static Worker Pool

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/file.h>

#define PORT 8080
#define BACKLOG 128
#define NUM_WORKERS 4
#define LOCK_FILE "/tmp/server.lock"

volatile sig_atomic_t shutdown_requested = 0;
pid_t worker_pids[NUM_WORKERS];

void sigterm_handler(int sig) {
    (void)sig;
    shutdown_requested = 1;
}

void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// Acquire accept lock (serialize accept calls)
int lock_accept(int lock_fd) {
    return flock(lock_fd, LOCK_EX);
}

// Release accept lock
int unlock_accept(int lock_fd) {
    return flock(lock_fd, LOCK_UN);
}

void worker_process(int listen_fd, int lock_fd, int worker_id) {
    char buffer[1024];
    ssize_t n;
    
    printf("Worker %d (PID %d) started\n", worker_id, getpid());
    
    while (!shutdown_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Acquire lock before accept
        if (lock_accept(lock_fd) < 0) {
            if (errno == EINTR) continue;
            perror("lock_accept");
            break;
        }
        
        // Accept connection
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        
        // Release lock immediately after accept
        unlock_accept(lock_fd);
        
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        
        printf("Worker %d handling connection\n", worker_id);
        
        // Handle client
        while ((n = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            
            // Echo back
            if (write(client_fd, buffer, n) != n) {
                perror("write");
                break;
            }
            
            if (strncmp(buffer, "quit", 4) == 0) {
                break;
            }
        }
        
        close(client_fd);
    }
    
    printf("Worker %d shutting down\n", worker_id);
}

int main() {
    int listen_fd, lock_fd;
    struct sockaddr_in server_addr;
    
    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);
    
    // Create lock file
    lock_fd = open(LOCK_FILE, O_RDWR | O_CREAT, 0644);
    if (lock_fd < 0) {
        perror("open lock file");
        exit(1);
    }
    
    // Create listening socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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
    
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Pre-fork server listening on port %d with %d workers\n", PORT, NUM_WORKERS);
    
    // Fork worker processes
    for (int i = 0; i < NUM_WORKERS; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        
        if (pid == 0) {
            // Child process (worker)
            worker_process(listen_fd, lock_fd, i);
            close(listen_fd);
            close(lock_fd);
            exit(0);
        }
        
        // Parent stores worker PID
        worker_pids[i] = pid;
    }
    
    // Parent process waits for shutdown signal
    while (!shutdown_requested) {
        pause();
    }
    
    printf("Shutting down server...\n");
    
    // Send SIGTERM to all workers
    for (int i = 0; i < NUM_WORKERS; i++) {
        kill(worker_pids[i], SIGTERM);
    }
    
    // Wait for all workers to exit
    for (int i = 0; i < NUM_WORKERS; i++) {
        waitpid(worker_pids[i], NULL, 0);
        printf("Worker %d terminated\n", i);
    }
    
    close(listen_fd);
    close(lock_fd);
    unlink(LOCK_FILE);
    
    printf("Server shutdown complete\n");
    return 0;
}
```

### Example 3: Dynamic Pre-fork Server (Apache-style)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <errno.h>

#define PORT 8080
#define MIN_WORKERS 2
#define MAX_WORKERS 10
#define MAX_REQUESTS_PER_CHILD 100

// Shared memory structure for worker status
typedef struct {
    pid_t pid;
    int busy;
    int requests_handled;
} worker_status_t;

typedef struct {
    int num_workers;
    worker_status_t workers[MAX_WORKERS];
} shared_data_t;

shared_data_t *shared;
int shmid;
volatile sig_atomic_t shutdown_requested = 0;

void cleanup_shared_memory() {
    if (shared != NULL) {
        shmdt(shared);
    }
    if (shmid >= 0) {
        shmctl(shmid, IPC_RMID, NULL);
    }
}

void sigterm_handler(int sig) {
    (void)sig;
    shutdown_requested = 1;
}

void worker_process(int listen_fd, int worker_idx) {
    char buffer[1024];
    int requests = 0;
    
    printf("Worker %d (PID %d) started\n", worker_idx, getpid());
    
    while (!shutdown_requested && requests < MAX_REQUESTS_PER_CHILD) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Mark as idle
        shared->workers[worker_idx].busy = 0;
        
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        
        // Mark as busy
        shared->workers[worker_idx].busy = 1;
        shared->workers[worker_idx].requests_handled++;
        requests++;
        
        // Simple request handling
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            write(client_fd, buffer, n);
        }
        
        close(client_fd);
    }
    
    printf("Worker %d exiting after %d requests\n", worker_idx, requests);
}

int count_idle_workers() {
    int idle = 0;
    for (int i = 0; i < shared->num_workers; i++) {
        if (shared->workers[i].pid > 0 && !shared->workers[i].busy) {
            idle++;
        }
    }
    return idle;
}

void spawn_worker(int listen_fd, int idx) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork");
        return;
    }
    
    if (pid == 0) {
        // Child
        worker_process(listen_fd, idx);
        exit(0);
    }
    
    // Parent
    shared->workers[idx].pid = pid;
    shared->workers[idx].busy = 0;
    shared->workers[idx].requests_handled = 0;
    shared->num_workers++;
}

int main() {
    int listen_fd;
    struct sockaddr_in server_addr;
    
    // Create shared memory
    shmid = shmget(IPC_PRIVATE, sizeof(shared_data_t), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(1);
    }
    
    shared = (shared_data_t*)shmat(shmid, NULL, 0);
    if (shared == (void*)-1) {
        perror("shmat");
        exit(1);
    }
    
    memset(shared, 0, sizeof(shared_data_t));
    atexit(cleanup_shared_memory);
    
    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    // Create socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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
    
    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Dynamic pre-fork server started on port %d\n", PORT);
    
    // Spawn initial workers
    for (int i = 0; i < MIN_WORKERS; i++) {
        spawn_worker(listen_fd, i);
    }
    
    // Management loop
    while (!shutdown_requested) {
        sleep(5); // Check every 5 seconds
        
        // Reap any dead workers
        pid_t pid;
        while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
            for (int i = 0; i < MAX_WORKERS; i++) {
                if (shared->workers[i].pid == pid) {
                    printf("Worker %d died, respawning\n", i);
                    spawn_worker(listen_fd, i);
                    break;
                }
            }
        }
        
        int idle = count_idle_workers();
        printf("Status: %d workers, %d idle\n", shared->num_workers, idle);
        
        // Spawn more workers if needed
        if (idle < 2 && shared->num_workers < MAX_WORKERS) {
            for (int i = 0; i < MAX_WORKERS && shared->num_workers < MAX_WORKERS; i++) {
                if (shared->workers[i].pid == 0) {
                    spawn_worker(listen_fd, i);
                    printf("Spawned additional worker (total: %d)\n", shared->num_workers);
                    break;
                }
            }
        }
        
        // Kill excess idle workers
        if (idle > 4 && shared->num_workers > MIN_WORKERS) {
            for (int i = MAX_WORKERS - 1; i >= 0 && idle > 4; i--) {
                if (shared->workers[i].pid > 0 && !shared->workers[i].busy) {
                    kill(shared->workers[i].pid, SIGTERM);
                    shared->workers[i].pid = 0;
                    shared->num_workers--;
                    idle--;
                    printf("Terminated idle worker (total: %d)\n", shared->num_workers);
                }
            }
        }
    }
    
    // Cleanup
    printf("Shutting down...\n");
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (shared->workers[i].pid > 0) {
            kill(shared->workers[i].pid, SIGTERM);
        }
    }
    
    while (wait(NULL) > 0);
    close(listen_fd);
    
    return 0;
}
```

---

## Rust Implementation Examples

### Example 1: Basic Fork-per-Connection Server

```rust
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::process;
use nix::unistd::{fork, ForkResult};
use nix::sys::wait::{waitpid, WaitPidFlag, WaitStatus};
use nix::sys::signal::{signal, SigHandler, Signal};

const PORT: u16 = 8080;

fn handle_client(mut stream: TcpStream) {
    let mut buffer = [0u8; 1024];
    
    println!("Worker {} handling connection", process::id());
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => break, // Connection closed
            Ok(n) => {
                let msg = String::from_utf8_lossy(&buffer[..n]);
                println!("Received: {}", msg);
                
                // Echo back
                if let Err(e) = stream.write_all(&buffer[..n]) {
                    eprintln!("Write error: {}", e);
                    break;
                }
                
                if msg.starts_with("quit") {
                    break;
                }
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }
    
    println!("Worker {} finished", process::id());
}

// SIGCHLD handler for reaping zombie processes
extern "C" fn sigchld_handler(_: i32) {
    loop {
        match waitpid(None, Some(WaitPidFlag::WNOHANG)) {
            Ok(WaitStatus::StillAlive) => break,
            Ok(_) => continue, // Reaped a child
            Err(_) => break,   // No more children
        }
    }
}

fn main() {
    // Install SIGCHLD handler
    unsafe {
        signal(Signal::SIGCHLD, SigHandler::Handler(sigchld_handler))
            .expect("Failed to set SIGCHLD handler");
    }
    
    let listener = TcpListener::bind(format!("0.0.0.0:{}", PORT))
        .expect("Failed to bind");
    
    println!("Server listening on port {}", PORT);
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                // Fork for each connection
                match unsafe { fork() } {
                    Ok(ForkResult::Parent { child }) => {
                        // Parent process
                        println!("Forked child process {}", child);
                        // Drop the stream in parent (child has its copy)
                        drop(stream);
                    }
                    Ok(ForkResult::Child) => {
                        // Child process
                        drop(listener); // Close listening socket in child
                        handle_client(stream);
                        process::exit(0);
                    }
                    Err(e) => {
                        eprintln!("Fork failed: {}", e);
                    }
                }
            }
            Err(e) => {
                eprintln!("Connection failed: {}", e);
            }
        }
    }
}
```

**Cargo.toml dependencies:**
```toml
[dependencies]
nix = { version = "0.27", features = ["process", "signal"] }
```

### Example 2: Pre-fork Server with Worker Pool

```rust
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::os::unix::io::AsRawFd;
use std::process;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::fs::File;
use nix::unistd::{fork, ForkResult};
use nix::sys::wait::{waitpid, WaitPidFlag};
use nix::sys::signal::{signal, SigHandler, Signal};
use nix::fcntl::{flock, FlockArg};

const PORT: u16 = 8080;
const NUM_WORKERS: usize = 4;

static SHUTDOWN: AtomicBool = AtomicBool::new(false);

extern "C" fn sigterm_handler(_: i32) {
    SHUTDOWN.store(true, Ordering::SeqCst);
}

extern "C" fn sigchld_handler(_: i32) {
    loop {
        match waitpid(None, Some(WaitPidFlag::WNOHANG)) {
            Ok(nix::sys::wait::WaitStatus::StillAlive) => break,
            Ok(_) => continue,
            Err(_) => break,
        }
    }
}

fn lock_accept(lock_file: &File) -> Result<(), String> {
    flock(lock_file.as_raw_fd(), FlockArg::LockExclusive)
        .map_err(|e| format!("Lock failed: {}", e))
}

fn unlock_accept(lock_file: &File) -> Result<(), String> {
    flock(lock_file.as_raw_fd(), FlockArg::Unlock)
        .map_err(|e| format!("Unlock failed: {}", e))
}

fn worker_process(listener: TcpListener, lock_file: File, worker_id: usize) {
    println!("Worker {} (PID {}) started", worker_id, process::id());
    
    while !SHUTDOWN.load(Ordering::SeqCst) {
        // Acquire lock before accept
        if let Err(e) = lock_accept(&lock_file) {
            eprintln!("Lock error: {}", e);
            break;
        }
        
        // Accept connection
        let result = listener.accept();
        
        // Release lock immediately
        let _ = unlock_accept(&lock_file);
        
        match result {
            Ok((stream, addr)) => {
                println!("Worker {} handling connection from {}", worker_id, addr);
                handle_client(stream);
            }
            Err(e) => {
                eprintln!("Accept error: {}", e);
            }
        }
    }
    
    println!("Worker {} shutting down", worker_id);
}

fn handle_client(mut stream: TcpStream) {
    let mut buffer = [0u8; 1024];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => break,
            Ok(n) => {
                if stream.write_all(&buffer[..n]).is_err() {
                    break;
                }
                
                let msg = String::from_utf8_lossy(&buffer[..n]);
                if msg.starts_with("quit") {
                    break;
                }
            }
            Err(_) => break,
        }
    }
}

fn main() {
    // Install signal handlers
    unsafe {
        signal(Signal::SIGTERM, SigHandler::Handler(sigterm_handler))
            .expect("Failed to set SIGTERM handler");
        signal(Signal::SIGINT, SigHandler::Handler(sigterm_handler))
            .expect("Failed to set SIGINT handler");
        signal(Signal::SIGCHLD, SigHandler::Handler(sigchld_handler))
            .expect("Failed to set SIGCHLD handler");
    }
    
    // Create lock file
    let lock_file = File::create("/tmp/rust_server.lock")
        .expect("Failed to create lock file");
    
    // Create listening socket
    let listener = TcpListener::bind(format!("0.0.0.0:{}", PORT))
        .expect("Failed to bind");
    
    println!("Pre-fork server listening on port {} with {} workers", PORT, NUM_WORKERS);
    
    let mut worker_pids = Vec::new();
    
    // Fork worker processes
    for i in 0..NUM_WORKERS {
        match unsafe { fork() } {
            Ok(ForkResult::Parent { child }) => {
                worker_pids.push(child);
            }
            Ok(ForkResult::Child) => {
                // Child process
                let lock_clone = File::open("/tmp/rust_server.lock")
                    .expect("Failed to open lock file");
                worker_process(listener, lock_clone, i);
                process::exit(0);
            }
            Err(e) => {
                eprintln!("Fork failed: {}", e);
                process::exit(1);
            }
        }
    }
    
    // Parent waits for shutdown
    while !SHUTDOWN.load(Ordering::SeqCst) {
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
    
    println!("Shutting down server...");
    
    // Send SIGTERM to all workers
    for pid in &worker_pids {
        let _ = nix::sys::signal::kill(*pid, Signal::SIGTERM);
    }
    
    // Wait for workers to exit
    for pid in worker_pids {
        let _ = waitpid(Some(pid), None);
    }
    
    println!("Server shutdown complete");
}
```

### Example 3: Modern Rust Approach with Process Pool

```rust
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::process::{Command, Child};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;
use std::thread;

const PORT: u16 = 8080;
const MIN_WORKERS: usize = 2;
const MAX_WORKERS: usize = 8;

struct WorkerManager {
    workers: Vec<Option<Child>>,
    shutdown: Arc<AtomicBool>,
}

impl WorkerManager {
    fn new(shutdown: Arc<AtomicBool>) -> Self {
        Self {
            workers: Vec::new(),
            shutdown,
        }
    }
    
    fn spawn_worker(&mut self) {
        let child = Command::new(std::env::current_exe().unwrap())
            .arg("--worker")
            .spawn()
            .expect("Failed to spawn worker");
        
        self.workers.push(Some(child));
        println!("Spawned worker (total: {})", self.count_active());
    }
    
    fn count_active(&self) -> usize {
        self.workers.iter().filter(|w| w.is_some()).count()
    }
    
    fn reap_dead_workers(&mut self) {
        for worker in &mut self.workers {
            if let Some(child) = worker {
                if let Ok(Some(_status)) = child.try_wait() {
                    *worker = None;
                }
            }
        }
        self.workers.retain(|w| w.is_some());
    }
    
    fn manage(&mut self) {
        while !self.shutdown.load(Ordering::SeqCst) {
            thread::sleep(Duration::from_secs(5));
            
            self.reap_dead_workers();
            let active = self.count_active();
            
            println!("Active workers: {}", active);
            
            // Spawn more workers if needed
            if active < MIN_WORKERS {
                while self.count_active() < MIN_WORKERS {
                    self.spawn_worker();
                }
            }
            
            // Could implement more sophisticated logic here:
            // - Monitor load and spawn up to MAX_WORKERS
            // - Kill excess idle workers
            // - Replace workers that have handled many requests
        }
    }
    
    fn shutdown_all(&mut self) {
        for worker in &mut self.workers {
            if let Some(mut child) = worker.take() {
                let _ = child.kill();
                let _ = child.wait();
            }
        }
    }
}

fn worker_main() {
    println!("Worker {} started", std::process::id());
    
    let listener = TcpListener::bind(format!("0.0.0.0:{}", PORT))
        .expect("Worker failed to bind");
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                handle_client(stream);
            }
            Err(e) => {
                eprintln!("Worker accept error: {}", e);
            }
        }
    }
}

fn handle_client(mut stream: TcpStream) {
    let mut buffer = [0u8; 1024];
    
    while let Ok(n) = stream.read(&mut buffer) {
        if n == 0 {
            break;
        }
        
        if stream.write_all(&buffer[..n]).is_err() {
            break;
        }
        
        let msg = String::from_utf8_lossy(&buffer[..n]);
        if msg.starts_with("quit") {
            break;
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    
    // Check if running as worker
    if args.len() > 1 && args[1] == "--worker" {
        worker_main();
        return;
    }
    
    // Master process
    println!("Starting master process");
    
    let shutdown = Arc::new(AtomicBool::new(false));
    let shutdown_clone = shutdown.clone();
    
    // Setup signal handler
    ctrlc::set_handler(move || {
        println!("Received shutdown signal");
        shutdown_clone.store(true, Ordering::SeqCst);
    }).expect("Error setting signal handler");
    
    let mut manager = WorkerManager::new(shutdown.clone());
    
    // Spawn initial workers
    for _ in 0..MIN_WORKERS {
        manager.spawn_worker();
    }
    
    // Management loop
    manager.manage();
    
    println!("Shutting down all workers...");
    manager.shutdown_all();
    println!("Shutdown complete");
}
```

**Additional Cargo.toml dependencies for this example:**
```toml
[dependencies]
nix = { version = "0.27", features = ["process", "signal"] }
ctrlc = "3.4"
```

---

## Performance Considerations

### Advantages of Fork-based Concurrency

1. **Process Isolation**
   - Memory corruption in one process doesn't affect others
   - Security: processes can't access each other's memory
   - Stability: crash in one worker doesn't bring down server

2. **Simplicity**
   - Straightforward programming model
   - No need for explicit synchronization
   - Each process has independent state

3. **Resource Control**
   - Can set per-process limits (CPU, memory, file descriptors)
   - Easy to monitor individual worker resource usage

4. **Multi-core Utilization**
   - OS scheduler distributes processes across cores
   - Automatic load balancing

### Disadvantages

1. **Memory Overhead**
   - Each process has full memory space (though COW helps)
   - Typically 1-2 MB minimum per process

2. **Fork Cost**
   - Creating process: 100-1000 microseconds
   - Much higher than thread creation (1-10 microseconds)
   - Pre-forking mitigates this

3. **Limited Scalability**
   - OS limits on number of processes (typically 1000-10000)
   - Context switching overhead with many processes

4. **Communication Overhead**
   - IPC more expensive than shared memory
   - Serialization needed for complex data

### When to Use Fork-based Concurrency

**Best suited for:**
- CPU-bound request processing
- Moderate connection rates (< 1000 concurrent)
- When isolation is critical (security, stability)
- Legacy code that's not thread-safe
- Systems with plentiful memory

**Alternatives:**
- **Threads**: Lower overhead, shared memory (requires synchronization)
- **Event loops** (epoll/kqueue): Single process, highest scalability
- **Async/await**: Modern approach, low overhead, complex programming model
- **Hybrid**: Pre-fork + event loop per process (nginx model)

### Optimization Tips

1. **Use Pre-forking**: Eliminate fork overhead during request handling
2. **Tune Worker Count**: Balance between parallelism and overhead
3. **Set Process Limits**: Prevent resource exhaustion
4. **Implement Graceful Restart**: Update code without downtime
5. **Monitor Workers**: Track and replace unhealthy processes
6. **Connection Limits**: Prevent overload by limiting per-worker connections
7. **Copy-on-Write**: Keep shared data read-only to maximize COW benefits

---

## Summary

Fork-based concurrency is a proven, robust approach to building concurrent TCP/IP servers using process-level parallelism. While not the most scalable solution compared to modern async/event-driven architectures, it offers significant advantages in terms of isolation, stability, and programming simplicity.

### Key Takeaways

1. **Fork-per-connection** is simple but has high overhead due to process creation costs
2. **Pre-fork models** eliminate fork overhead by maintaining a worker pool
3. **Dynamic pre-fork** adapts to load by spawning/killing workers as needed
4. **Process isolation** provides strong security and fault tolerance boundaries
5. **SIGCHLD handling** is critical to prevent zombie processes
6. **Serialized accept** prevents thundering herd with multiple workers
7. **Shared memory** enables coordination between processes when needed

### Modern Context

While fork-based servers are still used (Apache MPM prefork, some FastCGI servers), modern high-performance servers typically use:
- **Event-driven architectures** (nginx, Node.js)
- **Thread pools** (Apache MPM worker, Tomcat)
- **Async/await** (Tokio in Rust, asyncio in Python)
- **Hybrid models** (nginx: multiple workers with event loop per worker)

However, fork-based concurrency remains valuable for:
- Systems requiring strong isolation
- CPU-intensive request processing
- Interfacing with non-thread-safe libraries
- Educational purposes (understanding process management)
- Specific use cases where simplicity trumps maximum scalability

The choice between fork-based and other concurrency models depends on your specific requirements for scalability, resource usage, programming complexity, and isolation needs.

### Best Practices Summary

- Install SIGCHLD handler to reap zombie processes
- Use pre-fork for production servers
- Implement graceful shutdown with proper signal handling
- Set resource limits (RLIMIT_NPROC, RLIMIT_NOFILE)
- Monitor and restart workers that exceed resource limits
- Use accept serialization (lock/semaphore) with multiple workers
- Implement connection timeouts
- Log worker lifecycle events for debugging
- Consider hybrid approaches for high-scale applications

Fork-based concurrency represents a fundamental technique in Unix network programming, and understanding it provides valuable insights into process management, signal handling, and server architecture patterns that remain relevant even in modern async-heavy environments.