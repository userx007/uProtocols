# Thread-Safe WebSocket Handling

Thread-safe WebSocket handling is a critical aspect of building robust, concurrent applications that use WebSocket connections. When multiple threads need to send or receive WebSocket messages simultaneously, proper synchronization mechanisms are essential to prevent data corruption, race conditions, and undefined behavior.

## Core Concepts

**Thread Safety Challenges in WebSockets:**
WebSocket connections maintain stateful communication channels where:
- Multiple threads may attempt to send messages concurrently
- Incoming messages need to be distributed to various processing threads
- Connection state (open, closing, closed) must be synchronized
- Frame fragmentation and message assembly require atomic operations

**Synchronization Strategies:**
1. **Mutex Protection**: Traditional locking mechanisms to guard critical sections
2. **Lock-Free Queues**: Wait-free data structures for high-performance message passing
3. **Thread Synchronization**: Condition variables, atomic operations, and memory ordering

## C/C++ Implementation

Here's a comprehensive example using mutex protection and condition variables:

```c
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>

// Message structure for queue
typedef struct ws_message {
    char* data;
    size_t length;
    struct ws_message* next;
} ws_message_t;

// Thread-safe WebSocket connection
typedef struct {
    int socket_fd;
    pthread_mutex_t send_mutex;
    pthread_mutex_t recv_mutex;
    
    // Message queue for outgoing messages
    ws_message_t* queue_head;
    ws_message_t* queue_tail;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    
    // Connection state
    atomic_int state; // 0=connecting, 1=open, 2=closing, 3=closed
    atomic_bool running;
} ws_connection_t;

// Initialize WebSocket connection
int ws_connection_init(ws_connection_t* conn, int socket_fd) {
    conn->socket_fd = socket_fd;
    
    if (pthread_mutex_init(&conn->send_mutex, NULL) != 0) return -1;
    if (pthread_mutex_init(&conn->recv_mutex, NULL) != 0) return -1;
    if (pthread_mutex_init(&conn->queue_mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&conn->queue_cond, NULL) != 0) return -1;
    
    conn->queue_head = NULL;
    conn->queue_tail = NULL;
    atomic_store(&conn->state, 1); // open
    atomic_store(&conn->running, true);
    
    return 0;
}

// Thread-safe send function
int ws_send_message(ws_connection_t* conn, const char* data, size_t length) {
    if (atomic_load(&conn->state) != 1) {
        return -1; // Connection not open
    }
    
    pthread_mutex_lock(&conn->send_mutex);
    
    // Create WebSocket frame header (simplified)
    unsigned char header[10];
    size_t header_len = 0;
    
    header[0] = 0x81; // FIN bit set, text frame
    
    if (length <= 125) {
        header[1] = (unsigned char)length;
        header_len = 2;
    } else if (length <= 65535) {
        header[1] = 126;
        header[2] = (length >> 8) & 0xFF;
        header[3] = length & 0xFF;
        header_len = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (length >> (56 - i * 8)) & 0xFF;
        }
        header_len = 10;
    }
    
    // Send header and payload atomically
    ssize_t sent = send(conn->socket_fd, header, header_len, 0);
    if (sent > 0) {
        sent = send(conn->socket_fd, data, length, 0);
    }
    
    pthread_mutex_unlock(&conn->send_mutex);
    
    return (sent > 0) ? 0 : -1;
}

// Enqueue message for asynchronous sending
int ws_enqueue_message(ws_connection_t* conn, const char* data, size_t length) {
    ws_message_t* msg = malloc(sizeof(ws_message_t));
    if (!msg) return -1;
    
    msg->data = malloc(length);
    if (!msg->data) {
        free(msg);
        return -1;
    }
    
    memcpy(msg->data, data, length);
    msg->length = length;
    msg->next = NULL;
    
    pthread_mutex_lock(&conn->queue_mutex);
    
    if (conn->queue_tail) {
        conn->queue_tail->next = msg;
    } else {
        conn->queue_head = msg;
    }
    conn->queue_tail = msg;
    
    pthread_cond_signal(&conn->queue_cond);
    pthread_mutex_unlock(&conn->queue_mutex);
    
    return 0;
}

// Worker thread for sending queued messages
void* ws_send_worker(void* arg) {
    ws_connection_t* conn = (ws_connection_t*)arg;
    
    while (atomic_load(&conn->running)) {
        pthread_mutex_lock(&conn->queue_mutex);
        
        while (!conn->queue_head && atomic_load(&conn->running)) {
            pthread_cond_wait(&conn->queue_cond, &conn->queue_mutex);
        }
        
        ws_message_t* msg = conn->queue_head;
        if (msg) {
            conn->queue_head = msg->next;
            if (!conn->queue_head) {
                conn->queue_tail = NULL;
            }
        }
        
        pthread_mutex_unlock(&conn->queue_mutex);
        
        if (msg) {
            ws_send_message(conn, msg->data, msg->length);
            free(msg->data);
            free(msg);
        }
    }
    
    return NULL;
}

// Cleanup
void ws_connection_destroy(ws_connection_t* conn) {
    atomic_store(&conn->running, false);
    pthread_cond_signal(&conn->queue_cond);
    
    pthread_mutex_destroy(&conn->send_mutex);
    pthread_mutex_destroy(&conn->recv_mutex);
    pthread_mutex_destroy(&conn->queue_mutex);
    pthread_cond_destroy(&conn->queue_cond);
    
    // Free remaining messages
    ws_message_t* msg = conn->queue_head;
    while (msg) {
        ws_message_t* next = msg->next;
        free(msg->data);
        free(msg);
        msg = next;
    }
}
```

## C++ Implementation with Modern Features

```cpp
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <string>

class ThreadSafeWebSocket {
private:
    int socket_fd_;
    std::mutex send_mutex_;
    std::mutex recv_mutex_;
    
    // Lock-free state management
    std::atomic<bool> is_open_{true};
    std::atomic<bool> running_{true};
    
    // Message queue with mutex
    struct Message {
        std::vector<uint8_t> data;
        bool is_binary;
    };
    
    std::queue<Message> send_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    std::thread send_thread_;
    
    // Frame a message (simplified WebSocket framing)
    std::vector<uint8_t> frameMessage(const std::vector<uint8_t>& data, bool binary) {
        std::vector<uint8_t> frame;
        
        // Opcode: 0x1 for text, 0x2 for binary, with FIN bit
        frame.push_back(binary ? 0x82 : 0x81);
        
        size_t length = data.size();
        if (length <= 125) {
            frame.push_back(static_cast<uint8_t>(length));
        } else if (length <= 65535) {
            frame.push_back(126);
            frame.push_back((length >> 8) & 0xFF);
            frame.push_back(length & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((length >> (i * 8)) & 0xFF);
            }
        }
        
        frame.insert(frame.end(), data.begin(), data.end());
        return frame;
    }
    
    void sendWorker() {
        while (running_) {
            Message msg;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { 
                    return !send_queue_.empty() || !running_; 
                });
                
                if (!running_ && send_queue_.empty()) break;
                
                if (!send_queue_.empty()) {
                    msg = std::move(send_queue_.front());
                    send_queue_.pop();
                } else {
                    continue;
                }
            }
            
            sendMessageSync(msg.data, msg.is_binary);
        }
    }
    
    bool sendMessageSync(const std::vector<uint8_t>& data, bool binary) {
        if (!is_open_) return false;
        
        std::lock_guard<std::mutex> lock(send_mutex_);
        
        auto frame = frameMessage(data, binary);
        ssize_t sent = ::send(socket_fd_, frame.data(), frame.size(), 0);
        
        return sent == static_cast<ssize_t>(frame.size());
    }
    
public:
    ThreadSafeWebSocket(int socket_fd) 
        : socket_fd_(socket_fd),
          send_thread_(&ThreadSafeWebSocket::sendWorker, this) {
    }
    
    ~ThreadSafeWebSocket() {
        close();
    }
    
    // Async send - enqueues message
    bool sendAsync(const std::string& message, bool binary = false) {
        if (!is_open_) return false;
        
        std::vector<uint8_t> data(message.begin(), message.end());
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            send_queue_.push({std::move(data), binary});
        }
        
        queue_cv_.notify_one();
        return true;
    }
    
    // Sync send - immediate sending with mutex protection
    bool sendSync(const std::string& message, bool binary = false) {
        std::vector<uint8_t> data(message.begin(), message.end());
        return sendMessageSync(data, binary);
    }
    
    // Thread-safe receive (simplified)
    std::vector<uint8_t> receive() {
        std::lock_guard<std::mutex> lock(recv_mutex_);
        
        std::vector<uint8_t> buffer(4096);
        ssize_t received = ::recv(socket_fd_, buffer.data(), buffer.size(), 0);
        
        if (received > 0) {
            buffer.resize(received);
            return buffer;
        }
        
        return {};
    }
    
    void close() {
        if (is_open_.exchange(false)) {
            running_ = false;
            queue_cv_.notify_all();
            
            if (send_thread_.joinable()) {
                send_thread_.join();
            }
            
            ::close(socket_fd_);
        }
    }
    
    bool isOpen() const {
        return is_open_.load();
    }
};

// Usage example
int main() {
    int socket_fd = /* ... establish connection ... */ 0;
    ThreadSafeWebSocket ws(socket_fd);
    
    // Multiple threads can safely send
    std::thread t1([&ws]() {
        for (int i = 0; i < 100; ++i) {
            ws.sendAsync("Thread 1: Message " + std::to_string(i));
        }
    });
    
    std::thread t2([&ws]() {
        for (int i = 0; i < 100; ++i) {
            ws.sendAsync("Thread 2: Message " + std::to_string(i));
        }
    });
    
    t1.join();
    t2.join();
    
    return 0;
}
```

## Rust Implementation

Rust's ownership system provides excellent thread safety guarantees. Here's an implementation using `Arc`, `Mutex`, and channels:

```rust
use std::sync::{Arc, Mutex, Condvar};
use std::sync::atomic::{AtomicBool, Ordering};
use std::collections::VecDeque;
use std::thread;
use std::net::TcpStream;
use std::io::{Write, Read};

#[derive(Clone)]
struct Message {
    data: Vec<u8>,
    is_binary: bool,
}

pub struct ThreadSafeWebSocket {
    socket: Arc<Mutex<TcpStream>>,
    send_queue: Arc<(Mutex<VecDeque<Message>>, Condvar)>,
    is_open: Arc<AtomicBool>,
    running: Arc<AtomicBool>,
}

impl ThreadSafeWebSocket {
    pub fn new(stream: TcpStream) -> Self {
        let ws = ThreadSafeWebSocket {
            socket: Arc::new(Mutex::new(stream)),
            send_queue: Arc::new((Mutex::new(VecDeque::new()), Condvar::new())),
            is_open: Arc::new(AtomicBool::new(true)),
            running: Arc::new(AtomicBool::new(true)),
        };
        
        ws.start_send_worker();
        ws
    }
    
    fn start_send_worker(&self) {
        let socket = Arc::clone(&self.socket);
        let queue = Arc::clone(&self.send_queue);
        let running = Arc::clone(&self.running);
        let is_open = Arc::clone(&self.is_open);
        
        thread::spawn(move || {
            let (lock, cvar) = &*queue;
            
            while running.load(Ordering::SeqCst) {
                let message = {
                    let mut queue = lock.lock().unwrap();
                    
                    while queue.is_empty() && running.load(Ordering::SeqCst) {
                        queue = cvar.wait(queue).unwrap();
                    }
                    
                    if !running.load(Ordering::SeqCst) && queue.is_empty() {
                        break;
                    }
                    
                    queue.pop_front()
                };
                
                if let Some(msg) = message {
                    if is_open.load(Ordering::SeqCst) {
                        let _ = Self::send_frame(&socket, &msg.data, msg.is_binary);
                    }
                }
            }
        });
    }
    
    fn create_frame(data: &[u8], is_binary: bool) -> Vec<u8> {
        let mut frame = Vec::new();
        
        // FIN bit + opcode (0x1 for text, 0x2 for binary)
        frame.push(if is_binary { 0x82 } else { 0x81 });
        
        let len = data.len();
        if len <= 125 {
            frame.push(len as u8);
        } else if len <= 65535 {
            frame.push(126);
            frame.push((len >> 8) as u8);
            frame.push(len as u8);
        } else {
            frame.push(127);
            for i in (0..8).rev() {
                frame.push((len >> (i * 8)) as u8);
            }
        }
        
        frame.extend_from_slice(data);
        frame
    }
    
    fn send_frame(socket: &Arc<Mutex<TcpStream>>, data: &[u8], is_binary: bool) -> std::io::Result<()> {
        let frame = Self::create_frame(data, is_binary);
        let mut stream = socket.lock().unwrap();
        stream.write_all(&frame)?;
        stream.flush()
    }
    
    pub fn send_async(&self, message: &str) -> Result<(), &'static str> {
        if !self.is_open.load(Ordering::SeqCst) {
            return Err("WebSocket is closed");
        }
        
        let msg = Message {
            data: message.as_bytes().to_vec(),
            is_binary: false,
        };
        
        let (lock, cvar) = &*self.send_queue;
        let mut queue = lock.lock().unwrap();
        queue.push_back(msg);
        cvar.notify_one();
        
        Ok(())
    }
    
    pub fn send_sync(&self, message: &str) -> std::io::Result<()> {
        if !self.is_open.load(Ordering::SeqCst) {
            return Err(std::io::Error::new(
                std::io::ErrorKind::NotConnected,
                "WebSocket is closed"
            ));
        }
        
        Self::send_frame(&self.socket, message.as_bytes(), false)
    }
    
    pub fn receive(&self) -> std::io::Result<Vec<u8>> {
        let mut stream = self.socket.lock().unwrap();
        let mut buffer = vec![0u8; 4096];
        
        let n = stream.read(&mut buffer)?;
        buffer.truncate(n);
        
        Ok(buffer)
    }
    
    pub fn close(&self) {
        if self.is_open.swap(false, Ordering::SeqCst) {
            self.running.store(false, Ordering::SeqCst);
            
            let (_, cvar) = &*self.send_queue;
            cvar.notify_all();
        }
    }
    
    pub fn is_open(&self) -> bool {
        self.is_open.load(Ordering::SeqCst)
    }
}

// Automatically close when dropped
impl Drop for ThreadSafeWebSocket {
    fn drop(&mut self) {
        self.close();
    }
}

// Usage example
fn main() -> std::io::Result<()> {
    let stream = TcpStream::connect("127.0.0.1:8080")?;
    let ws = Arc::new(ThreadSafeWebSocket::new(stream));
    
    let ws1 = Arc::clone(&ws);
    let handle1 = thread::spawn(move || {
        for i in 0..100 {
            let _ = ws1.send_async(&format!("Thread 1: Message {}", i));
        }
    });
    
    let ws2 = Arc::clone(&ws);
    let handle2 = thread::spawn(move || {
        for i in 0..100 {
            let _ = ws2.send_async(&format!("Thread 2: Message {}", i));
        }
    });
    
    handle1.join().unwrap();
    handle2.join().unwrap();
    
    Ok(())
}
```

## Advanced: Lock-Free Queue Implementation (C++)

For high-performance scenarios, lock-free queues can eliminate mutex contention:

```cpp
#include <atomic>
#include <memory>

template<typename T>
class LockFreeQueue {
private:
    struct Node {
        std::shared_ptr<T> data;
        std::atomic<Node*> next;
        
        Node() : next(nullptr) {}
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    
public:
    LockFreeQueue() {
        Node* dummy = new Node();
        head_.store(dummy);
        tail_.store(dummy);
    }
    
    ~LockFreeQueue() {
        while (Node* old_head = head_.load()) {
            head_.store(old_head->next);
            delete old_head;
        }
    }
    
    void enqueue(T value) {
        auto new_data = std::make_shared<T>(std::move(value));
        Node* new_node = new Node();
        
        Node* old_tail = tail_.load();
        
        while (true) {
            Node* tail_next = old_tail->next.load();
            
            if (old_tail == tail_.load()) {
                if (tail_next == nullptr) {
                    if (old_tail->next.compare_exchange_weak(tail_next, new_node)) {
                        old_tail->data = new_data;
                        tail_.compare_exchange_weak(old_tail, new_node);
                        return;
                    }
                } else {
                    tail_.compare_exchange_weak(old_tail, tail_next);
                }
            }
            old_tail = tail_.load();
        }
    }
    
    std::shared_ptr<T> dequeue() {
        while (true) {
            Node* old_head = head_.load();
            Node* old_tail = tail_.load();
            Node* head_next = old_head->next.load();
            
            if (old_head == head_.load()) {
                if (old_head == old_tail) {
                    if (head_next == nullptr) {
                        return nullptr; // Queue is empty
                    }
                    tail_.compare_exchange_weak(old_tail, head_next);
                } else {
                    if (head_next != nullptr) {
                        auto data = head_next->data;
                        if (head_.compare_exchange_weak(old_head, head_next)) {
                            delete old_head;
                            return data;
                        }
                    }
                }
            }
        }
    }
};
```

## Summary

Thread-safe WebSocket handling requires careful synchronization to manage concurrent access to shared resources. The key approaches include:

**Mutex Protection**: Traditional locking with `pthread_mutex` (C), `std::mutex` (C++), or `Mutex` (Rust) provides straightforward thread safety but can introduce contention in high-throughput scenarios.

**Lock-Free Queues**: Using atomic operations and compare-and-swap techniques enables wait-free message passing with minimal contention, ideal for performance-critical applications.

**Thread Synchronization**: Condition variables (`pthread_cond`, `std::condition_variable`, `Condvar`) allow efficient thread coordination for producer-consumer patterns in message queuing.

**Atomic State Management**: Using atomic variables for connection state (`std::atomic`, `AtomicBool`) ensures thread-safe status checks without locks.

The choice of synchronization mechanism depends on your specific requirements: mutexes for simplicity and correctness, lock-free structures for maximum performance, and a combination of both for balanced real-world applications. Rust's ownership system provides compile-time guarantees that prevent many common threading bugs, while C/C++ requires more careful manual management but offers maximum control over performance characteristics.