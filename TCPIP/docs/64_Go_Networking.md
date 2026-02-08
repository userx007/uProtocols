# Go Networking: Comprehensive Guide

## Overview

Go (Golang) is exceptionally well-suited for network programming due to its built-in concurrency primitives (goroutines and channels), robust standard library, and simple syntax. The `net` package provides a portable interface for network I/O, including TCP/IP, UDP, domain name resolution, and Unix domain sockets.

## Table of Contents
1. [The net Package](#the-net-package)
2. [Goroutines and Concurrency](#goroutines-and-concurrency)
3. [TCP Programming](#tcp-programming)
4. [UDP Programming](#udp-programming)
5. [Concurrent Network Patterns](#concurrent-network-patterns)
6. [Advanced Topics](#advanced-topics)

---

## The net Package

The `net` package provides core networking functionality:

### Key Types and Interfaces

```go
// Core interfaces
type Conn interface {
    Read(b []byte) (n int, err error)
    Write(b []byte) (n int, err error)
    Close() error
    LocalAddr() Addr
    RemoteAddr() Addr
    SetDeadline(t time.Time) error
    SetReadDeadline(t time.Time) error
    SetWriteDeadline(t time.Time) error
}

type Listener interface {
    Accept() (Conn, error)
    Close() error
    Addr() Addr
}
```

---

## Goroutines and Concurrency

### Goroutines Basics

Goroutines are lightweight threads managed by the Go runtime. They enable concurrent execution with minimal overhead.

```go
package main

import (
    "fmt"
    "time"
)

func worker(id int) {
    fmt.Printf("Worker %d starting\n", id)
    time.Sleep(time.Second)
    fmt.Printf("Worker %d done\n", id)
}

func main() {
    // Launch 5 concurrent goroutines
    for i := 1; i <= 5; i++ {
        go worker(i)
    }
    
    // Wait for goroutines to complete
    time.Sleep(2 * time.Second)
    fmt.Println("All workers completed")
}
```

### Channels for Communication

```go
package main

import "fmt"

func sum(numbers []int, result chan int) {
    sum := 0
    for _, num := range numbers {
        sum += num
    }
    result <- sum // Send result to channel
}

func main() {
    numbers := []int{1, 2, 3, 4, 5, 6}
    result := make(chan int)
    
    // Split work between two goroutines
    go sum(numbers[:len(numbers)/2], result)
    go sum(numbers[len(numbers)/2:], result)
    
    // Receive results
    x, y := <-result, <-result
    fmt.Printf("Total sum: %d\n", x+y)
}
```

---

## TCP Programming

### 1. Simple TCP Echo Server

```go
package main

import (
    "bufio"
    "fmt"
    "log"
    "net"
)

func handleConnection(conn net.Conn) {
    defer conn.Close()
    
    fmt.Printf("Client connected: %s\n", conn.RemoteAddr())
    
    scanner := bufio.NewScanner(conn)
    for scanner.Scan() {
        text := scanner.Text()
        fmt.Printf("Received: %s\n", text)
        
        // Echo back to client
        conn.Write([]byte(text + "\n"))
    }
    
    if err := scanner.Err(); err != nil {
        log.Printf("Error reading from connection: %v\n", err)
    }
    
    fmt.Printf("Client disconnected: %s\n", conn.RemoteAddr())
}

func main() {
    listener, err := net.Listen("tcp", ":8080")
    if err != nil {
        log.Fatal(err)
    }
    defer listener.Close()
    
    fmt.Println("Server listening on :8080")
    
    for {
        conn, err := listener.Accept()
        if err != nil {
            log.Printf("Error accepting connection: %v\n", err)
            continue
        }
        
        // Handle each connection in a separate goroutine
        go handleConnection(conn)
    }
}
```

### 2. TCP Client

```go
package main

import (
    "bufio"
    "fmt"
    "log"
    "net"
    "os"
)

func main() {
    // Connect to server
    conn, err := net.Dial("tcp", "localhost:8080")
    if err != nil {
        log.Fatal(err)
    }
    defer conn.Close()
    
    fmt.Println("Connected to server")
    
    // Read user input and send to server
    go func() {
        scanner := bufio.NewScanner(os.Stdin)
        for scanner.Scan() {
            text := scanner.Text()
            fmt.Fprintf(conn, "%s\n", text)
        }
    }()
    
    // Read responses from server
    scanner := bufio.NewScanner(conn)
    for scanner.Scan() {
        fmt.Printf("Server: %s\n", scanner.Text())
    }
    
    if err := scanner.Err(); err != nil {
        log.Printf("Error reading from server: %v\n", err)
    }
}
```

### 3. HTTP Server (using net/http)

```go
package main

import (
    "encoding/json"
    "fmt"
    "log"
    "net/http"
    "sync"
    "time"
)

type Response struct {
    Message   string    `json:"message"`
    Timestamp time.Time `json:"timestamp"`
}

var (
    requestCount int
    mu           sync.Mutex
)

func helloHandler(w http.ResponseWriter, r *http.Request) {
    mu.Lock()
    requestCount++
    count := requestCount
    mu.Unlock()
    
    response := Response{
        Message:   fmt.Sprintf("Hello! Request #%d", count),
        Timestamp: time.Now(),
    }
    
    w.Header().Set("Content-Type", "application/json")
    json.NewEncoder(w).Encode(response)
}

func statsHandler(w http.ResponseWriter, r *http.Request) {
    mu.Lock()
    count := requestCount
    mu.Unlock()
    
    fmt.Fprintf(w, "Total requests: %d\n", count)
}

func main() {
    http.HandleFunc("/hello", helloHandler)
    http.HandleFunc("/stats", statsHandler)
    
    fmt.Println("Server starting on :8080")
    log.Fatal(http.ListenAndServe(":8080", nil))
}
```

---

## UDP Programming

### UDP Server

```go
package main

import (
    "fmt"
    "log"
    "net"
)

func main() {
    // Listen on UDP port 9000
    addr, err := net.ResolveUDPAddr("udp", ":9000")
    if err != nil {
        log.Fatal(err)
    }
    
    conn, err := net.ListenUDP("udp", addr)
    if err != nil {
        log.Fatal(err)
    }
    defer conn.Close()
    
    fmt.Println("UDP Server listening on :9000")
    
    buffer := make([]byte, 1024)
    
    for {
        n, clientAddr, err := conn.ReadFromUDP(buffer)
        if err != nil {
            log.Printf("Error reading: %v\n", err)
            continue
        }
        
        message := string(buffer[:n])
        fmt.Printf("Received from %s: %s\n", clientAddr, message)
        
        // Echo back
        response := []byte(fmt.Sprintf("Echo: %s", message))
        _, err = conn.WriteToUDP(response, clientAddr)
        if err != nil {
            log.Printf("Error writing: %v\n", err)
        }
    }
}
```

### UDP Client

```go
package main

import (
    "fmt"
    "log"
    "net"
    "time"
)

func main() {
    serverAddr, err := net.ResolveUDPAddr("udp", "localhost:9000")
    if err != nil {
        log.Fatal(err)
    }
    
    conn, err := net.DialUDP("udp", nil, serverAddr)
    if err != nil {
        log.Fatal(err)
    }
    defer conn.Close()
    
    // Send messages
    for i := 1; i <= 5; i++ {
        message := fmt.Sprintf("Message %d", i)
        _, err := conn.Write([]byte(message))
        if err != nil {
            log.Printf("Error sending: %v\n", err)
            continue
        }
        
        // Set read deadline
        conn.SetReadDeadline(time.Now().Add(2 * time.Second))
        
        buffer := make([]byte, 1024)
        n, err := conn.Read(buffer)
        if err != nil {
            log.Printf("Error receiving: %v\n", err)
            continue
        }
        
        fmt.Printf("Response: %s\n", string(buffer[:n]))
        time.Sleep(time.Second)
    }
}
```

---

## Concurrent Network Patterns

### 1. Worker Pool Pattern

```go
package main

import (
    "fmt"
    "log"
    "net"
    "time"
)

type Job struct {
    ID   int
    Conn net.Conn
}

func worker(id int, jobs <-chan Job, results chan<- int) {
    for job := range jobs {
        fmt.Printf("Worker %d processing job %d\n", id, job.ID)
        
        // Simulate work
        time.Sleep(time.Second)
        
        // Send response
        fmt.Fprintf(job.Conn, "Processed by worker %d\n", id)
        job.Conn.Close()
        
        results <- job.ID
    }
}

func main() {
    const numWorkers = 5
    jobs := make(chan Job, 100)
    results := make(chan int, 100)
    
    // Start worker pool
    for w := 1; w <= numWorkers; w++ {
        go worker(w, jobs, results)
    }
    
    // Start listener
    listener, err := net.Listen("tcp", ":8080")
    if err != nil {
        log.Fatal(err)
    }
    defer listener.Close()
    
    fmt.Printf("Server with %d workers listening on :8080\n", numWorkers)
    
    jobID := 0
    go func() {
        for {
            conn, err := listener.Accept()
            if err != nil {
                log.Printf("Error accepting: %v\n", err)
                continue
            }
            jobID++
            jobs <- Job{ID: jobID, Conn: conn}
        }
    }()
    
    // Collect results
    for i := 1; i <= 10; i++ {
        <-results
    }
}
```

### 2. Connection Pool

```go
package main

import (
    "errors"
    "fmt"
    "net"
    "sync"
    "time"
)

type ConnectionPool struct {
    connections chan net.Conn
    factory     func() (net.Conn, error)
    mu          sync.Mutex
    maxSize     int
    currentSize int
}

func NewConnectionPool(maxSize int, factory func() (net.Conn, error)) *ConnectionPool {
    return &ConnectionPool{
        connections: make(chan net.Conn, maxSize),
        factory:     factory,
        maxSize:     maxSize,
        currentSize: 0,
    }
}

func (p *ConnectionPool) Get() (net.Conn, error) {
    select {
    case conn := <-p.connections:
        return conn, nil
    default:
        p.mu.Lock()
        defer p.mu.Unlock()
        
        if p.currentSize < p.maxSize {
            conn, err := p.factory()
            if err != nil {
                return nil, err
            }
            p.currentSize++
            return conn, nil
        }
        
        return nil, errors.New("connection pool exhausted")
    }
}

func (p *ConnectionPool) Put(conn net.Conn) {
    select {
    case p.connections <- conn:
    default:
        conn.Close()
        p.mu.Lock()
        p.currentSize--
        p.mu.Unlock()
    }
}

func main() {
    pool := NewConnectionPool(5, func() (net.Conn, error) {
        return net.Dial("tcp", "localhost:8080")
    })
    
    // Use connections from pool
    for i := 0; i < 10; i++ {
        go func(id int) {
            conn, err := pool.Get()
            if err != nil {
                fmt.Printf("Worker %d: %v\n", id, err)
                return
            }
            
            fmt.Fprintf(conn, "Request from worker %d\n", id)
            time.Sleep(time.Second)
            
            pool.Put(conn)
        }(i)
    }
    
    time.Sleep(5 * time.Second)
}
```

### 3. Rate Limiter

```go
package main

import (
    "fmt"
    "log"
    "net"
    "time"
)

type RateLimiter struct {
    tokens chan struct{}
}

func NewRateLimiter(requestsPerSecond int) *RateLimiter {
    rl := &RateLimiter{
        tokens: make(chan struct{}, requestsPerSecond),
    }
    
    // Refill tokens
    go func() {
        ticker := time.NewTicker(time.Second / time.Duration(requestsPerSecond))
        defer ticker.Stop()
        
        for range ticker.C {
            select {
            case rl.tokens <- struct{}{}:
            default:
            }
        }
    }()
    
    return rl
}

func (rl *RateLimiter) Allow() bool {
    select {
    case <-rl.tokens:
        return true
    default:
        return false
    }
}

func handleRateLimitedConnection(conn net.Conn, limiter *RateLimiter) {
    defer conn.Close()
    
    if !limiter.Allow() {
        conn.Write([]byte("Rate limit exceeded\n"))
        return
    }
    
    conn.Write([]byte("Request processed\n"))
}

func main() {
    limiter := NewRateLimiter(10) // 10 requests per second
    
    listener, err := net.Listen("tcp", ":8080")
    if err != nil {
        log.Fatal(err)
    }
    defer listener.Close()
    
    fmt.Println("Rate-limited server listening on :8080")
    
    for {
        conn, err := listener.Accept()
        if err != nil {
            log.Printf("Error accepting: %v\n", err)
            continue
        }
        
        go handleRateLimitedConnection(conn, limiter)
    }
}
```

---

## Advanced Topics

### 1. Timeout Handling

```go
package main

import (
    "fmt"
    "log"
    "net"
    "time"
)

func handleWithTimeout(conn net.Conn) {
    defer conn.Close()
    
    // Set overall connection timeout
    conn.SetDeadline(time.Now().Add(30 * time.Second))
    
    // Set read timeout
    conn.SetReadDeadline(time.Now().Add(5 * time.Second))
    
    buffer := make([]byte, 1024)
    n, err := conn.Read(buffer)
    if err != nil {
        if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
            fmt.Println("Read timeout occurred")
            conn.Write([]byte("Timeout\n"))
            return
        }
        log.Printf("Read error: %v\n", err)
        return
    }
    
    fmt.Printf("Received: %s\n", string(buffer[:n]))
    
    // Set write timeout
    conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
    conn.Write([]byte("Response\n"))
}

func main() {
    listener, err := net.Listen("tcp", ":8080")
    if err != nil {
        log.Fatal(err)
    }
    defer listener.Close()
    
    for {
        conn, err := listener.Accept()
        if err != nil {
            continue
        }
        go handleWithTimeout(conn)
    }
}
```

### 2. Context-Based Cancellation

```go
package main

import (
    "context"
    "fmt"
    "io"
    "log"
    "net"
    "time"
)

func handleWithContext(ctx context.Context, conn net.Conn) {
    defer conn.Close()
    
    // Create a channel for completion
    done := make(chan error, 1)
    
    go func() {
        buffer := make([]byte, 1024)
        n, err := conn.Read(buffer)
        if err != nil {
            done <- err
            return
        }
        
        fmt.Printf("Received: %s\n", string(buffer[:n]))
        _, err = conn.Write([]byte("Response\n"))
        done <- err
    }()
    
    select {
    case <-ctx.Done():
        fmt.Println("Request cancelled")
        conn.Write([]byte("Cancelled\n"))
    case err := <-done:
        if err != nil && err != io.EOF {
            log.Printf("Error: %v\n", err)
        }
    }
}

func main() {
    listener, err := net.Listen("tcp", ":8080")
    if err != nil {
        log.Fatal(err)
    }
    defer listener.Close()
    
    fmt.Println("Context-aware server listening on :8080")
    
    for {
        conn, err := listener.Accept()
        if err != nil {
            continue
        }
        
        // Create context with timeout
        ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
        
        go func() {
            handleWithContext(ctx, conn)
            cancel()
        }()
    }
}
```

### 3. Multiplexed Server

```go
package main

import (
    "bufio"
    "fmt"
    "log"
    "net"
    "strings"
    "sync"
)

type ChatRoom struct {
    clients map[net.Conn]string
    mu      sync.RWMutex
    join    chan net.Conn
    leave   chan net.Conn
    message chan string
}

func NewChatRoom() *ChatRoom {
    cr := &ChatRoom{
        clients: make(map[net.Conn]string),
        join:    make(chan net.Conn),
        leave:   make(chan net.Conn),
        message: make(chan string),
    }
    
    go cr.run()
    return cr
}

func (cr *ChatRoom) run() {
    for {
        select {
        case conn := <-cr.join:
            cr.mu.Lock()
            cr.clients[conn] = conn.RemoteAddr().String()
            cr.mu.Unlock()
            fmt.Printf("Client joined: %s\n", conn.RemoteAddr())
            
        case conn := <-cr.leave:
            cr.mu.Lock()
            delete(cr.clients, conn)
            cr.mu.Unlock()
            conn.Close()
            fmt.Printf("Client left: %s\n", conn.RemoteAddr())
            
        case msg := <-cr.message:
            cr.mu.RLock()
            for conn := range cr.clients {
                fmt.Fprintf(conn, "%s\n", msg)
            }
            cr.mu.RUnlock()
        }
    }
}

func (cr *ChatRoom) handleClient(conn net.Conn) {
    defer func() {
        cr.leave <- conn
    }()
    
    cr.join <- conn
    
    scanner := bufio.NewScanner(conn)
    for scanner.Scan() {
        text := strings.TrimSpace(scanner.Text())
        if text == "" {
            continue
        }
        
        cr.message <- fmt.Sprintf("%s: %s", conn.RemoteAddr(), text)
    }
}

func main() {
    chatRoom := NewChatRoom()
    
    listener, err := net.Listen("tcp", ":8080")
    if err != nil {
        log.Fatal(err)
    }
    defer listener.Close()
    
    fmt.Println("Chat server listening on :8080")
    
    for {
        conn, err := listener.Accept()
        if err != nil {
            log.Printf("Error accepting: %v\n", err)
            continue
        }
        
        go chatRoom.handleClient(conn)
    }
}
```

---

## Best Practices

1. **Always close connections**: Use `defer conn.Close()` to ensure cleanup
2. **Handle errors properly**: Network errors are common and should be handled gracefully
3. **Use goroutines for concurrent handling**: Each connection should be handled in its own goroutine
4. **Set timeouts**: Prevent resource exhaustion from hanging connections
5. **Use buffered I/O**: `bufio` package improves performance for text protocols
6. **Implement graceful shutdown**: Use context and signal handling for clean shutdowns
7. **Pool connections**: Reuse connections when making multiple requests to the same server
8. **Monitor goroutine leaks**: Ensure all goroutines eventually terminate

## Summary

Go's networking capabilities combine simplicity with power. The `net` package provides low-level control when needed, while goroutines make concurrent network programming natural and efficient. This makes Go an excellent choice for building scalable network services, from simple TCP servers to complex distributed systems.