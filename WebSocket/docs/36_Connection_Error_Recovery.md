# WebSocket Connection Error Recovery

## Overview

Connection error recovery is a critical aspect of building robust WebSocket applications. Unlike HTTP's stateless request-response model, WebSockets maintain persistent connections that are vulnerable to various network issues: temporary disconnections, broken pipes, connection resets, DNS failures, and infrastructure problems. Effective error recovery ensures your application remains resilient and provides a good user experience even when network conditions are less than ideal.

## Types of Connection Errors

**Network-Level Errors:**
- **Broken Pipes (EPIPE/SIGPIPE)**: Occur when writing to a socket whose remote end has closed
- **Connection Resets (ECONNRESET)**: The remote peer abruptly terminated the connection
- **Timeouts (ETIMEDOUT)**: No response received within the expected timeframe
- **Network Unreachable (ENETUNREACH)**: Routing issues prevent packet delivery

**Application-Level Errors:**
- **Protocol Violations**: Invalid frames or handshake failures
- **Ping/Pong Timeouts**: Heartbeat mechanism failures indicating a dead connection
- **Close Frame Errors**: Abnormal closure without proper handshake

**Infrastructure Errors:**
- **DNS Resolution Failures**: Cannot resolve the server hostname
- **TLS/SSL Errors**: Certificate validation or encryption issues
- **Proxy/Firewall Issues**: Intermediaries dropping or blocking connections

## Error Recovery Strategies

1. **Exponential Backoff**: Progressively increase delay between reconnection attempts
2. **Connection Pooling**: Maintain multiple connections for redundancy
3. **Heartbeat Monitoring**: Detect dead connections proactively
4. **Message Queuing**: Buffer messages during disconnection
5. **Graceful Degradation**: Fall back to alternative communication methods

## C Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

#define MAX_BACKOFF 32000  // 32 seconds max backoff
#define INITIAL_BACKOFF 1000  // 1 second initial backoff
#define MAX_RETRIES 10
#define PING_INTERVAL 30  // seconds
#define PONG_TIMEOUT 10   // seconds

typedef enum {
    CONN_DISCONNECTED,
    CONN_CONNECTING,
    CONN_CONNECTED,
    CONN_ERROR,
    CONN_RECONNECTING
} ConnectionState;

typedef struct {
    int sockfd;
    ConnectionState state;
    int retry_count;
    int backoff_ms;
    time_t last_ping;
    time_t last_pong;
    char host[256];
    int port;
    bool should_reconnect;
} WebSocketConnection;

// Message queue for buffering during disconnection
typedef struct MessageNode {
    char *data;
    size_t len;
    struct MessageNode *next;
} MessageNode;

typedef struct {
    MessageNode *head;
    MessageNode *tail;
    size_t count;
} MessageQueue;

// Ignore SIGPIPE to handle broken pipes gracefully
void setup_signal_handlers() {
    signal(SIGPIPE, SIG_IGN);
}

// Initialize message queue
void queue_init(MessageQueue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
}

// Enqueue message
bool queue_push(MessageQueue *q, const char *data, size_t len) {
    MessageNode *node = malloc(sizeof(MessageNode));
    if (!node) return false;
    
    node->data = malloc(len);
    if (!node->data) {
        free(node);
        return false;
    }
    
    memcpy(node->data, data, len);
    node->len = len;
    node->next = NULL;
    
    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }
    q->count++;
    return true;
}

// Dequeue message
MessageNode* queue_pop(MessageQueue *q) {
    if (!q->head) return NULL;
    
    MessageNode *node = q->head;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    return node;
}

// Calculate next backoff delay with exponential backoff
int calculate_backoff(int current_backoff) {
    int next = current_backoff * 2;
    return (next > MAX_BACKOFF) ? MAX_BACKOFF : next;
}

// Initialize WebSocket connection structure
void ws_connection_init(WebSocketConnection *conn, const char *host, int port) {
    conn->sockfd = -1;
    conn->state = CONN_DISCONNECTED;
    conn->retry_count = 0;
    conn->backoff_ms = INITIAL_BACKOFF;
    conn->last_ping = 0;
    conn->last_pong = 0;
    strncpy(conn->host, host, sizeof(conn->host) - 1);
    conn->port = port;
    conn->should_reconnect = true;
}

// Handle specific socket errors
const char* get_error_description(int error_code) {
    switch (error_code) {
        case EPIPE: return "Broken pipe - remote closed connection";
        case ECONNRESET: return "Connection reset by peer";
        case ETIMEDOUT: return "Connection timed out";
        case ENETUNREACH: return "Network unreachable";
        case EHOSTUNREACH: return "Host unreachable";
        case ECONNREFUSED: return "Connection refused";
        default: return strerror(error_code);
    }
}

// Attempt to connect to WebSocket server
int ws_connect(WebSocketConnection *conn) {
    struct addrinfo hints, *result, *rp;
    char port_str[6];
    int sock = -1;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    snprintf(port_str, sizeof(port_str), "%d", conn->port);
    
    // Resolve hostname
    if (getaddrinfo(conn->host, port_str, &hints, &result) != 0) {
        fprintf(stderr, "DNS resolution failed for %s\n", conn->host);
        return -1;
    }
    
    // Try each address until success
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;
        
        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  // Success
        }
        
        fprintf(stderr, "Connect failed: %s\n", get_error_description(errno));
        close(sock);
        sock = -1;
    }
    
    freeaddrinfo(result);
    
    if (sock == -1) {
        fprintf(stderr, "Failed to connect to %s:%d\n", conn->host, conn->port);
        return -1;
    }
    
    conn->sockfd = sock;
    return 0;
}

// Send data with error handling
ssize_t ws_send_with_recovery(WebSocketConnection *conn, const char *data, 
                               size_t len, MessageQueue *queue) {
    if (conn->state != CONN_CONNECTED) {
        // Queue message for later delivery
        if (queue && queue_push(queue, data, len)) {
            printf("Message queued (connection down)\n");
            return len;
        }
        return -1;
    }
    
    ssize_t sent = send(conn->sockfd, data, len, MSG_NOSIGNAL);
    
    if (sent < 0) {
        int error = errno;
        fprintf(stderr, "Send error: %s\n", get_error_description(error));
        
        // Handle different error types
        switch (error) {
            case EPIPE:
            case ECONNRESET:
            case ETIMEDOUT:
                // Connection is dead, trigger reconnection
                conn->state = CONN_ERROR;
                if (queue) queue_push(queue, data, len);
                break;
            
            case EAGAIN:
            case EWOULDBLOCK:
                // Temporary error, can retry
                if (queue) queue_push(queue, data, len);
                break;
            
            default:
                fprintf(stderr, "Unhandled send error\n");
                break;
        }
    }
    
    return sent;
}

// Receive data with error detection
ssize_t ws_recv_with_detection(WebSocketConnection *conn, char *buffer, 
                                size_t len) {
    ssize_t received = recv(conn->sockfd, buffer, len, 0);
    
    if (received < 0) {
        int error = errno;
        fprintf(stderr, "Recv error: %s\n", get_error_description(error));
        
        if (error != EAGAIN && error != EWOULDBLOCK) {
            conn->state = CONN_ERROR;
        }
    } else if (received == 0) {
        // Graceful close
        printf("Connection closed by peer\n");
        conn->state = CONN_DISCONNECTED;
    }
    
    return received;
}

// Reconnection logic with exponential backoff
bool ws_reconnect(WebSocketConnection *conn, MessageQueue *queue) {
    if (conn->retry_count >= MAX_RETRIES) {
        fprintf(stderr, "Max retries exceeded\n");
        conn->should_reconnect = false;
        return false;
    }
    
    conn->state = CONN_RECONNECTING;
    
    // Wait with exponential backoff
    printf("Reconnecting in %d ms (attempt %d/%d)...\n", 
           conn->backoff_ms, conn->retry_count + 1, MAX_RETRIES);
    usleep(conn->backoff_ms * 1000);
    
    // Close old socket if open
    if (conn->sockfd >= 0) {
        close(conn->sockfd);
        conn->sockfd = -1;
    }
    
    // Attempt reconnection
    if (ws_connect(conn) == 0) {
        printf("Reconnected successfully!\n");
        conn->state = CONN_CONNECTED;
        conn->retry_count = 0;
        conn->backoff_ms = INITIAL_BACKOFF;
        conn->last_ping = time(NULL);
        conn->last_pong = time(NULL);
        
        // Flush queued messages
        MessageNode *node;
        while ((node = queue_pop(queue)) != NULL) {
            send(conn->sockfd, node->data, node->len, MSG_NOSIGNAL);
            free(node->data);
            free(node);
        }
        
        return true;
    }
    
    // Reconnection failed
    conn->retry_count++;
    conn->backoff_ms = calculate_backoff(conn->backoff_ms);
    return false;
}

// Heartbeat check
void ws_heartbeat_check(WebSocketConnection *conn) {
    if (conn->state != CONN_CONNECTED) return;
    
    time_t now = time(NULL);
    
    // Send ping if interval elapsed
    if (now - conn->last_ping >= PING_INTERVAL) {
        char ping[] = "PING";
        if (send(conn->sockfd, ping, sizeof(ping), MSG_NOSIGNAL) < 0) {
            conn->state = CONN_ERROR;
            return;
        }
        conn->last_ping = now;
    }
    
    // Check pong timeout
    if (now - conn->last_pong > PING_INTERVAL + PONG_TIMEOUT) {
        fprintf(stderr, "Pong timeout - connection dead\n");
        conn->state = CONN_ERROR;
    }
}

// Example usage
int main() {
    setup_signal_handlers();
    
    WebSocketConnection conn;
    MessageQueue queue;
    queue_init(&queue);
    
    ws_connection_init(&conn, "echo.websocket.org", 80);
    
    // Initial connection
    if (ws_connect(&conn) == 0) {
        conn.state = CONN_CONNECTED;
        printf("Connected successfully\n");
    }
    
    // Main loop with error recovery
    while (conn.should_reconnect) {
        if (conn.state == CONN_ERROR || conn.state == CONN_DISCONNECTED) {
            ws_reconnect(&conn, &queue);
            continue;
        }
        
        if (conn.state == CONN_CONNECTED) {
            ws_heartbeat_check(&conn);
            
            // Simulate sending data
            const char *msg = "Hello, WebSocket!";
            ws_send_with_recovery(&conn, msg, strlen(msg), &queue);
            
            sleep(1);
        }
    }
    
    // Cleanup
    if (conn.sockfd >= 0) close(conn.sockfd);
    
    return 0;
}
```

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <queue>
#include <memory>
#include <chrono>
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>
#include <system_error>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>

class WebSocketException : public std::runtime_error {
public:
    enum class ErrorType {
        BROKEN_PIPE,
        CONNECTION_RESET,
        TIMEOUT,
        NETWORK_UNREACHABLE,
        DNS_FAILURE,
        PROTOCOL_ERROR,
        UNKNOWN
    };

    WebSocketException(ErrorType type, const std::string& msg)
        : std::runtime_error(msg), error_type_(type) {}

    ErrorType type() const { return error_type_; }
    
    bool is_recoverable() const {
        return error_type_ == ErrorType::BROKEN_PIPE ||
               error_type_ == ErrorType::CONNECTION_RESET ||
               error_type_ == ErrorType::TIMEOUT ||
               error_type_ == ErrorType::NETWORK_UNREACHABLE;
    }

private:
    ErrorType error_type_;
};

class ConnectionPolicy {
public:
    virtual ~ConnectionPolicy() = default;
    virtual int next_backoff_ms(int current_backoff, int retry_count) = 0;
    virtual bool should_retry(int retry_count) = 0;
};

class ExponentialBackoffPolicy : public ConnectionPolicy {
public:
    ExponentialBackoffPolicy(int initial_ms = 1000, int max_ms = 32000, 
                             int max_retries = 10)
        : initial_backoff_ms_(initial_ms)
        , max_backoff_ms_(max_ms)
        , max_retries_(max_retries) {}

    int next_backoff_ms(int current_backoff, int retry_count) override {
        if (retry_count == 0) return initial_backoff_ms_;
        int next = current_backoff * 2;
        return std::min(next, max_backoff_ms_);
    }

    bool should_retry(int retry_count) override {
        return retry_count < max_retries_;
    }

private:
    int initial_backoff_ms_;
    int max_backoff_ms_;
    int max_retries_;
};

class MessageBuffer {
public:
    struct Message {
        std::string data;
        std::chrono::steady_clock::time_point timestamp;
    };

    void push(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push({data, std::chrono::steady_clock::now()});
    }

    bool pop(Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) return false;
        msg = buffer_.front();
        buffer_.pop();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!buffer_.empty()) buffer_.pop();
    }

private:
    std::queue<Message> buffer_;
    mutable std::mutex mutex_;
};

class WebSocketConnection {
public:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        ERROR,
        RECONNECTING
    };

    using ErrorCallback = std::function<void(const WebSocketException&)>;
    using StateCallback = std::function<void(State, State)>;

    WebSocketConnection(const std::string& host, int port,
                       std::unique_ptr<ConnectionPolicy> policy)
        : host_(host)
        , port_(port)
        , sockfd_(-1)
        , state_(State::DISCONNECTED)
        , retry_count_(0)
        , current_backoff_ms_(0)
        , policy_(std::move(policy))
        , should_run_(false) {
        signal(SIGPIPE, SIG_IGN);
    }

    ~WebSocketConnection() {
        disconnect();
    }

    void set_error_callback(ErrorCallback cb) { error_callback_ = cb; }
    void set_state_callback(StateCallback cb) { state_callback_ = cb; }

    bool connect() {
        change_state(State::CONNECTING);

        struct addrinfo hints{}, *result, *rp;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        std::string port_str = std::to_string(port_);
        if (getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result) != 0) {
            handle_error(WebSocketException::ErrorType::DNS_FAILURE,
                        "DNS resolution failed for " + host_);
            return false;
        }

        for (rp = result; rp != nullptr; rp = rp->ai_next) {
            sockfd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sockfd_ < 0) continue;

            // Set timeouts
            struct timeval timeout{5, 0};
            setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            if (::connect(sockfd_, rp->ai_addr, rp->ai_addrlen) == 0) {
                break;
            }

            close(sockfd_);
            sockfd_ = -1;
        }

        freeaddrinfo(result);

        if (sockfd_ < 0) {
            handle_error(WebSocketException::ErrorType::CONNECTION_RESET,
                        "Failed to connect to " + host_ + ":" + std::to_string(port_));
            return false;
        }

        change_state(State::CONNECTED);
        retry_count_ = 0;
        current_backoff_ms_ = 0;
        last_activity_ = std::chrono::steady_clock::now();

        flush_message_buffer();
        return true;
    }

    void disconnect() {
        should_run_ = false;
        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }
        change_state(State::DISCONNECTED);
    }

    bool send(const std::string& data) {
        if (state_ != State::CONNECTED) {
            message_buffer_.push(data);
            std::cout << "Message buffered (state: " << static_cast<int>(state_) << ")\n";
            return false;
        }

        ssize_t sent = ::send(sockfd_, data.c_str(), data.size(), MSG_NOSIGNAL);

        if (sent < 0) {
            handle_send_error(errno, data);
            return false;
        }

        last_activity_ = std::chrono::steady_clock::now();
        return true;
    }

    std::string receive(size_t max_len = 4096) {
        if (state_ != State::CONNECTED) {
            return "";
        }

        std::vector<char> buffer(max_len);
        ssize_t received = ::recv(sockfd_, buffer.data(), max_len, 0);

        if (received < 0) {
            handle_recv_error(errno);
            return "";
        } else if (received == 0) {
            change_state(State::DISCONNECTED);
            return "";
        }

        last_activity_ = std::chrono::steady_clock::now();
        return std::string(buffer.data(), received);
    }

    bool reconnect() {
        if (!policy_->should_retry(retry_count_)) {
            std::cerr << "Max retries exceeded\n";
            return false;
        }

        change_state(State::RECONNECTING);

        current_backoff_ms_ = policy_->next_backoff_ms(current_backoff_ms_, retry_count_);
        
        std::cout << "Reconnecting in " << current_backoff_ms_ << "ms (attempt "
                  << retry_count_ + 1 << ")...\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(current_backoff_ms_));

        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }

        retry_count_++;

        if (connect()) {
            std::cout << "Reconnected successfully!\n";
            return true;
        }

        return false;
    }

    void run_with_recovery() {
        should_run_ = true;

        if (!connect()) {
            while (should_run_ && !reconnect()) {
                // Keep trying to reconnect
            }
        }

        while (should_run_) {
            if (state_ == State::ERROR || state_ == State::DISCONNECTED) {
                reconnect();
                continue;
            }

            if (state_ == State::CONNECTED) {
                check_connection_health();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    State get_state() const { return state_; }
    size_t buffered_messages() const { return message_buffer_.size(); }

private:
    void change_state(State new_state) {
        State old_state = state_;
        state_ = new_state;
        if (state_callback_) {
            state_callback_(old_state, new_state);
        }
    }

    void handle_error(WebSocketException::ErrorType type, const std::string& msg) {
        WebSocketException ex(type, msg);
        std::cerr << "Error: " << msg << "\n";
        
        if (error_callback_) {
            error_callback_(ex);
        }

        if (ex.is_recoverable()) {
            change_state(State::ERROR);
        }
    }

    void handle_send_error(int error_code, const std::string& data) {
        WebSocketException::ErrorType type;
        std::string msg;

        switch (error_code) {
            case EPIPE:
                type = WebSocketException::ErrorType::BROKEN_PIPE;
                msg = "Broken pipe - remote closed connection";
                message_buffer_.push(data);
                break;
            case ECONNRESET:
                type = WebSocketException::ErrorType::CONNECTION_RESET;
                msg = "Connection reset by peer";
                message_buffer_.push(data);
                break;
            case ETIMEDOUT:
                type = WebSocketException::ErrorType::TIMEOUT;
                msg = "Send timeout";
                message_buffer_.push(data);
                break;
            default:
                type = WebSocketException::ErrorType::UNKNOWN;
                msg = std::string("Send error: ") + strerror(error_code);
                break;
        }

        handle_error(type, msg);
    }

    void handle_recv_error(int error_code) {
        if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
            return; // Temporary, not fatal
        }

        WebSocketException::ErrorType type;
        std::string msg;

        switch (error_code) {
            case ECONNRESET:
                type = WebSocketException::ErrorType::CONNECTION_RESET;
                msg = "Connection reset during receive";
                break;
            case ETIMEDOUT:
                type = WebSocketException::ErrorType::TIMEOUT;
                msg = "Receive timeout";
                break;
            default:
                type = WebSocketException::ErrorType::UNKNOWN;
                msg = std::string("Receive error: ") + strerror(error_code);
                break;
        }

        handle_error(type, msg);
    }

    void flush_message_buffer() {
        MessageBuffer::Message msg;
        while (message_buffer_.pop(msg)) {
            std::cout << "Flushing buffered message\n";
            ::send(sockfd_, msg.data.c_str(), msg.data.size(), MSG_NOSIGNAL);
        }
    }

    void check_connection_health() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_activity_).count();

        if (elapsed > 60) {
            std::cout << "Connection idle for " << elapsed << "s, sending heartbeat\n";
            send("PING");
        }
    }

    std::string host_;
    int port_;
    int sockfd_;
    std::atomic<State> state_;
    int retry_count_;
    int current_backoff_ms_;
    std::unique_ptr<ConnectionPolicy> policy_;
    MessageBuffer message_buffer_;
    ErrorCallback error_callback_;
    StateCallback state_callback_;
    std::chrono::steady_clock::time_point last_activity_;
    std::atomic<bool> should_run_;
};

// Example usage
int main() {
    auto policy = std::make_unique<ExponentialBackoffPolicy>(1000, 32000, 10);
    WebSocketConnection conn("echo.websocket.org", 80, std::move(policy));

    conn.set_error_callback([](const WebSocketException& ex) {
        std::cerr << "Error occurred: " << ex.what() 
                  << " (recoverable: " << ex.is_recoverable() << ")\n";
    });

    conn.set_state_callback([](auto old_state, auto new_state) {
        std::cout << "State changed: " << static_cast<int>(old_state)
                  << " -> " << static_cast<int>(new_state) << "\n";
    });

    // Run in separate thread
    std::thread conn_thread([&conn]() {
        conn.run_with_recovery();
    });

    // Simulate sending messages
    for (int i = 0; i < 100; ++i) {
        std::string msg = "Message " + std::to_string(i);
        conn.send(msg);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    conn.disconnect();
    conn_thread.join();

    return 0;
}
```

## Rust Implementation

```rust
use std::collections::VecDeque;
use std::error::Error;
use std::fmt;
use std::io::{self, ErrorKind, Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

// Custom error types for WebSocket operations
#[derive(Debug, Clone)]
pub enum WebSocketError {
    BrokenPipe,
    ConnectionReset,
    Timeout,
    NetworkUnreachable,
    DnsFailure(String),
    ProtocolError(String),
    IoError(String),
}

impl fmt::Display for WebSocketError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            WebSocketError::BrokenPipe => write!(f, "Broken pipe - remote closed connection"),
            WebSocketError::ConnectionReset => write!(f, "Connection reset by peer"),
            WebSocketError::Timeout => write!(f, "Connection timeout"),
            WebSocketError::NetworkUnreachable => write!(f, "Network unreachable"),
            WebSocketError::DnsFailure(msg) => write!(f, "DNS failure: {}", msg),
            WebSocketError::ProtocolError(msg) => write!(f, "Protocol error: {}", msg),
            WebSocketError::IoError(msg) => write!(f, "I/O error: {}", msg),
        }
    }
}

impl Error for WebSocketError {}

impl WebSocketError {
    pub fn is_recoverable(&self) -> bool {
        matches!(
            self,
            WebSocketError::BrokenPipe
                | WebSocketError::ConnectionReset
                | WebSocketError::Timeout
                | WebSocketError::NetworkUnreachable
        )
    }

    pub fn from_io_error(e: &io::Error) -> Self {
        match e.kind() {
            ErrorKind::BrokenPipe => WebSocketError::BrokenPipe,
            ErrorKind::ConnectionReset => WebSocketError::ConnectionReset,
            ErrorKind::TimedOut => WebSocketError::Timeout,
            ErrorKind::NetworkUnreachable => WebSocketError::NetworkUnreachable,
            _ => WebSocketError::IoError(e.to_string()),
        }
    }
}

// Connection policy trait for different retry strategies
pub trait ConnectionPolicy: Send {
    fn next_backoff(&mut self, retry_count: u32) -> Duration;
    fn should_retry(&self, retry_count: u32) -> bool;
    fn reset(&mut self);
}

// Exponential backoff policy
pub struct ExponentialBackoff {
    initial: Duration,
    max: Duration,
    max_retries: u32,
    current: Duration,
}

impl ExponentialBackoff {
    pub fn new(initial: Duration, max: Duration, max_retries: u32) -> Self {
        Self {
            initial,
            max,
            max_retries,
            current: initial,
        }
    }
}

impl ConnectionPolicy for ExponentialBackoff {
    fn next_backoff(&mut self, retry_count: u32) -> Duration {
        if retry_count == 0 {
            self.current = self.initial;
        } else {
            self.current = std::cmp::min(self.current * 2, self.max);
        }
        self.current
    }

    fn should_retry(&self, retry_count: u32) -> bool {
        retry_count < self.max_retries
    }

    fn reset(&mut self) {
        self.current = self.initial;
    }
}

// Message buffer for queuing during disconnection
#[derive(Clone)]
struct BufferedMessage {
    data: Vec<u8>,
    timestamp: Instant,
}

pub struct MessageBuffer {
    buffer: VecDeque<BufferedMessage>,
    max_size: usize,
}

impl MessageBuffer {
    pub fn new(max_size: usize) -> Self {
        Self {
            buffer: VecDeque::with_capacity(max_size),
            max_size,
        }
    }

    pub fn push(&mut self, data: Vec<u8>) -> bool {
        if self.buffer.len() >= self.max_size {
            return false;
        }
        self.buffer.push_back(BufferedMessage {
            data,
            timestamp: Instant::now(),
        });
        true
    }

    pub fn pop(&mut self) -> Option<BufferedMessage> {
        self.buffer.pop_front()
    }

    pub fn len(&self) -> usize {
        self.buffer.len()
    }

    pub fn clear(&mut self) {
        self.buffer.clear();
    }
}

// Connection state
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error,
    Reconnecting,
}

// WebSocket connection with error recovery
pub struct WebSocketConnection {
    host: String,
    port: u16,
    stream: Option<TcpStream>,
    state: ConnectionState,
    retry_count: u32,
    policy: Box<dyn ConnectionPolicy>,
    message_buffer: Arc<Mutex<MessageBuffer>>,
    last_activity: Instant,
    error_callback: Option<Box<dyn Fn(&WebSocketError) + Send + Sync>>,
    state_callback: Option<Box<dyn Fn(ConnectionState, ConnectionState) + Send + Sync>>,
}

impl WebSocketConnection {
    pub fn new(
        host: String,
        port: u16,
        policy: Box<dyn ConnectionPolicy>,
    ) -> Self {
        Self {
            host,
            port,
            stream: None,
            state: ConnectionState::Disconnected,
            retry_count: 0,
            policy,
            message_buffer: Arc::new(Mutex::new(MessageBuffer::new(1000))),
            last_activity: Instant::now(),
            error_callback: None,
            state_callback: None,
        }
    }

    pub fn set_error_callback<F>(&mut self, callback: F)
    where
        F: Fn(&WebSocketError) + Send + Sync + 'static,
    {
        self.error_callback = Some(Box::new(callback));
    }

    pub fn set_state_callback<F>(&mut self, callback: F)
    where
        F: Fn(ConnectionState, ConnectionState) + Send + Sync + 'static,
    {
        self.state_callback = Some(Box::new(callback));
    }

    fn change_state(&mut self, new_state: ConnectionState) {
        let old_state = self.state;
        self.state = new_state;
        if let Some(ref callback) = self.state_callback {
            callback(old_state, new_state);
        }
    }

    fn handle_error(&mut self, error: WebSocketError) {
        eprintln!("Error: {}", error);
        
        if let Some(ref callback) = self.error_callback {
            callback(&error);
        }

        if error.is_recoverable() {
            self.change_state(ConnectionState::Error);
        }
    }

    pub fn connect(&mut self) -> Result<(), WebSocketError> {
        self.change_state(ConnectionState::Connecting);

        let addr = format!("{}:{}", self.host, self.port);
        let socket_addrs = addr.to_socket_addrs()
            .map_err(|e| WebSocketError::DnsFailure(e.to_string()))?;

        let mut last_err = None;
        for addr in socket_addrs {
            match TcpStream::connect_timeout(&addr, Duration::from_secs(5)) {
                Ok(mut stream) => {
                    // Set socket options
                    stream.set_read_timeout(Some(Duration::from_secs(5)))
                        .map_err(|e| WebSocketError::IoError(e.to_string()))?;
                    stream.set_write_timeout(Some(Duration::from_secs(5)))
                        .map_err(|e| WebSocketError::IoError(e.to_string()))?;
                    stream.set_nodelay(true)
                        .map_err(|e| WebSocketError::IoError(e.to_string()))?;

                    self.stream = Some(stream);
                    self.change_state(ConnectionState::Connected);
                    self.retry_count = 0;
                    self.policy.reset();
                    self.last_activity = Instant::now();

                    self.flush_message_buffer()?;
                    println!("Connected to {}:{}", self.host, self.port);
                    return Ok(());
                }
                Err(e) => {
                    eprintln!("Failed to connect to {}: {}", addr, e);
                    last_err = Some(e);
                }
            }
        }

        let err = WebSocketError::from_io_error(
            last_err.as_ref().unwrap()
        );
        self.handle_error(err.clone());
        Err(err)
    }

    pub fn send(&mut self, data: &[u8]) -> Result<usize, WebSocketError> {
        if self.state != ConnectionState::Connected {
            // Buffer the message
            let mut buffer = self.message_buffer.lock().unwrap();
            if buffer.push(data.to_vec()) {
                println!("Message buffered (state: {:?})", self.state);
            } else {
                eprintln!("Message buffer full, dropping message");
            }
            return Err(WebSocketError::ProtocolError("Not connected".to_string()));
        }

        let stream = self.stream.as_mut().ok_or_else(|| {
            WebSocketError::ProtocolError("No stream available".to_string())
        })?;

        match stream.write(data) {
            Ok(n) => {
                self.last_activity = Instant::now();
                Ok(n)
            }
            Err(e) => {
                let error = WebSocketError::from_io_error(&e);
                
                // Buffer the message for retry
                if error.is_recoverable() {
                    let mut buffer = self.message_buffer.lock().unwrap();
                    buffer.push(data.to_vec());
                }
                
                self.handle_error(error.clone());
                Err(error)
            }
        }
    }

    pub fn receive(&mut self, buffer: &mut [u8]) -> Result<usize, WebSocketError> {
        if self.state != ConnectionState::Connected {
            return Err(WebSocketError::ProtocolError("Not connected".to_string()));
        }

        let stream = self.stream.as_mut().ok_or_else(|| {
            WebSocketError::ProtocolError("No stream available".to_string())
        })?;

        match stream.read(buffer) {
            Ok(0) => {
                println!("Connection closed by peer");
                self.change_state(ConnectionState::Disconnected);
                Err(WebSocketError::ConnectionReset)
            }
            Ok(n) => {
                self.last_activity = Instant::now();
                Ok(n)
            }
            Err(e) if e.kind() == ErrorKind::WouldBlock || e.kind() == ErrorKind::TimedOut => {
                Ok(0) // Non-fatal
            }
            Err(e) => {
                let error = WebSocketError::from_io_error(&e);
                self.handle_error(error.clone());
                Err(error)
            }
        }
    }

    pub fn reconnect(&mut self) -> Result<(), WebSocketError> {
        if !self.policy.should_retry(self.retry_count) {
            eprintln!("Max retries exceeded");
            return Err(WebSocketError::ProtocolError("Max retries exceeded".to_string()));
        }

        self.change_state(ConnectionState::Reconnecting);

        let backoff = self.policy.next_backoff(self.retry_count);
        println!(
            "Reconnecting in {:?} (attempt {})...",
            backoff,
            self.retry_count + 1
        );

        thread::sleep(backoff);

        // Close old connection
        self.stream = None;

        self.retry_count += 1;

        self.connect()
    }

    pub fn disconnect(&mut self) {
        self.stream = None;
        self.change_state(ConnectionState::Disconnected);
        println!("Disconnected");
    }

    fn flush_message_buffer(&mut self) -> Result<(), WebSocketError> {
        let mut buffer = self.message_buffer.lock().unwrap();
        while let Some(msg) = buffer.pop() {
            println!("Flushing buffered message ({} bytes)", msg.data.len());
            if let Some(ref mut stream) = self.stream {
                stream.write_all(&msg.data)
                    .map_err(|e| WebSocketError::from_io_error(&e))?;
            }
        }
        Ok(())
    }

    pub fn check_connection_health(&mut self) {
        if self.state != ConnectionState::Connected {
            return;
        }

        let elapsed = self.last_activity.elapsed();
        if elapsed > Duration::from_secs(60) {
            println!("Connection idle for {:?}, sending heartbeat", elapsed);
            let _ = self.send(b"PING");
        }
    }

    pub fn get_state(&self) -> ConnectionState {
        self.state
    }

    pub fn buffered_messages(&self) -> usize {
        self.message_buffer.lock().unwrap().len()
    }
}

// Example usage
fn main() -> Result<(), Box<dyn Error>> {
    let policy = Box::new(ExponentialBackoff::new(
        Duration::from_secs(1),
        Duration::from_secs(32),
        10,
    ));

    let mut conn = WebSocketConnection::new(
        "echo.websocket.org".to_string(),
        80,
        policy,
    );

    conn.set_error_callback(|err| {
        eprintln!("Error callback: {} (recoverable: {})", err, err.is_recoverable());
    });

    conn.set_state_callback(|old_state, new_state| {
        println!("State changed: {:?} -> {:?}", old_state, new_state);
    });

    // Initial connection
    if conn.connect().is_err() {
        // Attempt reconnection
        while conn.reconnect().is_err() {
            if conn.get_state() == ConnectionState::Disconnected {
                break;
            }
        }
    }

    // Main loop with error recovery
    for i in 0..100 {
        match conn.get_state() {
            ConnectionState::Error | ConnectionState::Disconnected => {
                let _ = conn.reconnect();
                continue;
            }
            ConnectionState::Connected => {
                conn.check_connection_health();

                let msg = format!("Message {}", i);
                if let Err(e) = conn.send(msg.as_bytes()) {
                    eprintln!("Send failed: {}", e);
                }

                // Simulate receiving
                let mut buffer = [0u8; 4096];
                match conn.receive(&mut buffer) {
                    Ok(n) if n > 0 => {
                        println!("Received {} bytes", n);
                    }
                    Ok(_) => {}
                    Err(e) => eprintln!("Receive failed: {}", e),
                }

                thread::sleep(Duration::from_secs(1));
            }
            _ => {
                thread::sleep(Duration::from_millis(100));
            }
        }
    }

    conn.disconnect();
    Ok(())
}
```

## Summary

**WebSocket Connection Error Recovery** is essential for building production-ready applications that maintain persistent connections. The key aspects covered include:

### Core Concepts

1. **Error Types**: Understanding different categories of errors (network-level, application-level, infrastructure) helps determine appropriate recovery strategies

2. **Recovery Strategies**: Implementing exponential backoff prevents overwhelming servers, while message queuing ensures no data loss during temporary outages

3. **State Management**: Tracking connection state (Disconnected, Connecting, Connected, Error, Reconnecting) enables proper error handling and user feedback

### Implementation Patterns

**C Implementation** demonstrates low-level socket error handling with manual retry logic, signal handling for SIGPIPE, and direct errno inspection for granular error classification.

**C++ Implementation** provides object-oriented abstractions with policy-based retry strategies, exception-based error handling, thread-safe message buffering, and callback mechanisms for extensibility.

**Rust Implementation** leverages Rust's type system for compile-time safety, uses Result types for explicit error propagation, implements trait-based policies for flexibility, and ensures thread safety through Arc and Mutex primitives.

### Best Practices

- **Exponential Backoff**: Start with short delays (1s) and increase progressively to avoid server overload
- **Maximum Retries**: Set reasonable limits to prevent infinite retry loops
- **Message Buffering**: Queue messages during disconnection for automatic replay
- **Heartbeat Monitoring**: Proactively detect dead connections using ping/pong mechanisms
- **Error Classification**: Distinguish between recoverable and fatal errors
- **User Feedback**: Notify users of connection status changes through callbacks or events
- **Graceful Degradation**: Provide fallback mechanisms when WebSocket connections fail

These patterns ensure your WebSocket applications remain resilient in the face of network instability, providing reliable service to users even under adverse conditions.