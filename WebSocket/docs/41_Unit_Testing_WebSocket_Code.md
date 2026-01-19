# Unit Testing WebSocket Code

## Overview

Unit testing WebSocket code presents unique challenges because WebSockets maintain stateful, bidirectional connections with external servers. Effective testing requires isolating the code under test from actual network dependencies, creating predictable test scenarios, and verifying both sending and receiving behaviors. This is accomplished through mock connections, dependency injection, and careful abstraction of WebSocket functionality.

## Key Testing Challenges

**Stateful Connections**: WebSockets maintain connection state (connecting, open, closing, closed), making state transitions critical to test.

**Asynchronous Behavior**: Messages arrive asynchronously, requiring tests to handle timing and concurrency.

**External Dependencies**: Real WebSocket servers introduce flakiness, latency, and configuration complexity in tests.

**Bidirectional Communication**: Tests must verify both outgoing messages and responses to incoming messages.

## Core Testing Strategies

### 1. Dependency Injection

Create interfaces or abstract classes that define WebSocket operations, allowing real implementations to be swapped with mocks during testing.

### 2. Mock Connections

Implement fake WebSocket connections that simulate network behavior without actual I/O.

### 3. State Verification

Test state transitions explicitly: connection establishment, message handling, error scenarios, and graceful shutdown.

### 4. Event-Driven Testing

Verify that callbacks and event handlers are invoked correctly with expected data.

## C++ Implementation

```cpp
// websocket_interface.h
#ifndef WEBSOCKET_INTERFACE_H
#define WEBSOCKET_INTERFACE_H

#include <string>
#include <functional>
#include <memory>

// Abstract interface for WebSocket operations
class IWebSocket {
public:
    virtual ~IWebSocket() = default;
    
    using MessageCallback = std::function<void(const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ConnectionCallback = std::function<void()>;
    
    virtual bool connect(const std::string& url) = 0;
    virtual void send(const std::string& message) = 0;
    virtual void close() = 0;
    virtual bool isConnected() const = 0;
    
    virtual void onMessage(MessageCallback callback) = 0;
    virtual void onError(ErrorCallback callback) = 0;
    virtual void onOpen(ConnectionCallback callback) = 0;
    virtual void onClose(ConnectionCallback callback) = 0;
};

// Mock WebSocket for testing
class MockWebSocket : public IWebSocket {
private:
    bool connected_ = false;
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    ConnectionCallback open_callback_;
    ConnectionCallback close_callback_;
    std::vector<std::string> sent_messages_;
    
public:
    bool connect(const std::string& url) override {
        connected_ = true;
        if (open_callback_) {
            open_callback_();
        }
        return true;
    }
    
    void send(const std::string& message) override {
        if (!connected_) {
            throw std::runtime_error("Cannot send on disconnected socket");
        }
        sent_messages_.push_back(message);
    }
    
    void close() override {
        connected_ = false;
        if (close_callback_) {
            close_callback_();
        }
    }
    
    bool isConnected() const override {
        return connected_;
    }
    
    void onMessage(MessageCallback callback) override {
        message_callback_ = callback;
    }
    
    void onError(ErrorCallback callback) override {
        error_callback_ = callback;
    }
    
    void onOpen(ConnectionCallback callback) override {
        open_callback_ = callback;
    }
    
    void onClose(ConnectionCallback callback) override {
        close_callback_ = callback;
    }
    
    // Test helper methods
    void simulateMessage(const std::string& message) {
        if (message_callback_) {
            message_callback_(message);
        }
    }
    
    void simulateError(const std::string& error) {
        if (error_callback_) {
            error_callback_(error);
        }
    }
    
    const std::vector<std::string>& getSentMessages() const {
        return sent_messages_;
    }
    
    void clearSentMessages() {
        sent_messages_.clear();
    }
};

// Business logic class using dependency injection
class ChatClient {
private:
    std::shared_ptr<IWebSocket> websocket_;
    std::vector<std::string> received_messages_;
    bool is_ready_ = false;
    
public:
    explicit ChatClient(std::shared_ptr<IWebSocket> websocket)
        : websocket_(websocket) {
        
        websocket_->onOpen([this]() {
            is_ready_ = true;
        });
        
        websocket_->onMessage([this](const std::string& msg) {
            received_messages_.push_back(msg);
        });
        
        websocket_->onClose([this]() {
            is_ready_ = false;
        });
    }
    
    bool connect(const std::string& url) {
        return websocket_->connect(url);
    }
    
    void sendMessage(const std::string& message) {
        if (!is_ready_) {
            throw std::runtime_error("Client not ready");
        }
        websocket_->send(message);
    }
    
    const std::vector<std::string>& getReceivedMessages() const {
        return received_messages_;
    }
    
    bool isReady() const {
        return is_ready_;
    }
};

#endif // WEBSOCKET_INTERFACE_H
```

```cpp
// test_websocket.cpp
#include <cassert>
#include <iostream>
#include "websocket_interface.h"

void testConnectionLifecycle() {
    auto mock = std::make_shared<MockWebSocket>();
    ChatClient client(mock);
    
    // Test initial state
    assert(!client.isReady());
    
    // Test connection
    bool connected = client.connect("ws://localhost:8080");
    assert(connected);
    assert(client.isReady());
    
    // Test close
    mock->close();
    assert(!client.isReady());
    
    std::cout << "✓ Connection lifecycle test passed\n";
}

void testMessageSending() {
    auto mock = std::make_shared<MockWebSocket>();
    ChatClient client(mock);
    
    client.connect("ws://localhost:8080");
    
    // Send messages
    client.sendMessage("Hello");
    client.sendMessage("World");
    
    // Verify sent messages
    const auto& sent = mock->getSentMessages();
    assert(sent.size() == 2);
    assert(sent[0] == "Hello");
    assert(sent[1] == "World");
    
    std::cout << "✓ Message sending test passed\n";
}

void testMessageReceiving() {
    auto mock = std::make_shared<MockWebSocket>();
    ChatClient client(mock);
    
    client.connect("ws://localhost:8080");
    
    // Simulate receiving messages
    mock->simulateMessage("Message 1");
    mock->simulateMessage("Message 2");
    
    // Verify received messages
    const auto& received = client.getReceivedMessages();
    assert(received.size() == 2);
    assert(received[0] == "Message 1");
    assert(received[1] == "Message 2");
    
    std::cout << "✓ Message receiving test passed\n";
}

void testSendBeforeConnect() {
    auto mock = std::make_shared<MockWebSocket>();
    ChatClient client(mock);
    
    // Attempt to send before connecting
    bool exception_thrown = false;
    try {
        client.sendMessage("Test");
    } catch (const std::runtime_error& e) {
        exception_thrown = true;
    }
    
    assert(exception_thrown);
    
    std::cout << "✓ Send before connect test passed\n";
}

int main() {
    testConnectionLifecycle();
    testMessageSending();
    testMessageReceiving();
    testSendBeforeConnect();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
```

## Rust Implementation

```rust
// websocket_interface.rs
use std::sync::{Arc, Mutex};

// Trait defining WebSocket operations
pub trait WebSocketInterface: Send + Sync {
    fn connect(&mut self, url: &str) -> Result<(), String>;
    fn send(&mut self, message: &str) -> Result<(), String>;
    fn close(&mut self) -> Result<(), String>;
    fn is_connected(&self) -> bool;
    
    // Simulate receiving a message (for testing)
    fn on_message(&mut self, callback: Box<dyn Fn(String) + Send + Sync>);
    fn on_open(&mut self, callback: Box<dyn Fn() + Send + Sync>);
    fn on_close(&mut self, callback: Box<dyn Fn() + Send + Sync>);
    fn on_error(&mut self, callback: Box<dyn Fn(String) + Send + Sync>);
}

// Mock WebSocket implementation for testing
pub struct MockWebSocket {
    connected: bool,
    sent_messages: Vec<String>,
    message_callback: Option<Box<dyn Fn(String) + Send + Sync>>,
    open_callback: Option<Box<dyn Fn() + Send + Sync>>,
    close_callback: Option<Box<dyn Fn() + Send + Sync>>,
    error_callback: Option<Box<dyn Fn(String) + Send + Sync>>,
}

impl MockWebSocket {
    pub fn new() -> Self {
        MockWebSocket {
            connected: false,
            sent_messages: Vec::new(),
            message_callback: None,
            open_callback: None,
            close_callback: None,
            error_callback: None,
        }
    }
    
    // Test helper methods
    pub fn simulate_message(&self, message: String) {
        if let Some(ref callback) = self.message_callback {
            callback(message);
        }
    }
    
    pub fn simulate_error(&self, error: String) {
        if let Some(ref callback) = self.error_callback {
            callback(error);
        }
    }
    
    pub fn get_sent_messages(&self) -> &[String] {
        &self.sent_messages
    }
    
    pub fn clear_sent_messages(&mut self) {
        self.sent_messages.clear();
    }
}

impl WebSocketInterface for MockWebSocket {
    fn connect(&mut self, _url: &str) -> Result<(), String> {
        self.connected = true;
        if let Some(ref callback) = self.open_callback {
            callback();
        }
        Ok(())
    }
    
    fn send(&mut self, message: &str) -> Result<(), String> {
        if !self.connected {
            return Err("Cannot send on disconnected socket".to_string());
        }
        self.sent_messages.push(message.to_string());
        Ok(())
    }
    
    fn close(&mut self) -> Result<(), String> {
        self.connected = false;
        if let Some(ref callback) = self.close_callback {
            callback();
        }
        Ok(())
    }
    
    fn is_connected(&self) -> bool {
        self.connected
    }
    
    fn on_message(&mut self, callback: Box<dyn Fn(String) + Send + Sync>) {
        self.message_callback = Some(callback);
    }
    
    fn on_open(&mut self, callback: Box<dyn Fn() + Send + Sync>) {
        self.open_callback = Some(callback);
    }
    
    fn on_close(&mut self, callback: Box<dyn Fn() + Send + Sync>) {
        self.close_callback = Some(callback);
    }
    
    fn on_error(&mut self, callback: Box<dyn Fn(String) + Send + Sync>) {
        self.error_callback = Some(callback);
    }
}

// Business logic using dependency injection
pub struct ChatClient {
    websocket: Arc<Mutex<dyn WebSocketInterface>>,
    received_messages: Arc<Mutex<Vec<String>>>,
    is_ready: Arc<Mutex<bool>>,
}

impl ChatClient {
    pub fn new(websocket: Arc<Mutex<dyn WebSocketInterface>>) -> Self {
        let received_messages = Arc::new(Mutex::new(Vec::new()));
        let is_ready = Arc::new(Mutex::new(false));
        
        // Setup callbacks
        let received_clone = Arc::clone(&received_messages);
        let ready_clone = Arc::clone(&is_ready);
        let ready_clone2 = Arc::clone(&is_ready);
        
        let mut ws = websocket.lock().unwrap();
        
        ws.on_open(Box::new(move || {
            *ready_clone.lock().unwrap() = true;
        }));
        
        ws.on_message(Box::new(move |msg| {
            received_clone.lock().unwrap().push(msg);
        }));
        
        ws.on_close(Box::new(move || {
            *ready_clone2.lock().unwrap() = false;
        }));
        
        drop(ws);
        
        ChatClient {
            websocket,
            received_messages,
            is_ready,
        }
    }
    
    pub fn connect(&self, url: &str) -> Result<(), String> {
        self.websocket.lock().unwrap().connect(url)
    }
    
    pub fn send_message(&self, message: &str) -> Result<(), String> {
        if !*self.is_ready.lock().unwrap() {
            return Err("Client not ready".to_string());
        }
        self.websocket.lock().unwrap().send(message)
    }
    
    pub fn get_received_messages(&self) -> Vec<String> {
        self.received_messages.lock().unwrap().clone()
    }
    
    pub fn is_ready(&self) -> bool {
        *self.is_ready.lock().unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_connection_lifecycle() {
        let mock = Arc::new(Mutex::new(MockWebSocket::new()));
        let client = ChatClient::new(mock.clone());
        
        // Test initial state
        assert!(!client.is_ready());
        
        // Test connection
        assert!(client.connect("ws://localhost:8080").is_ok());
        assert!(client.is_ready());
        
        // Test close
        mock.lock().unwrap().close().unwrap();
        assert!(!client.is_ready());
    }
    
    #[test]
    fn test_message_sending() {
        let mock = Arc::new(Mutex::new(MockWebSocket::new()));
        let client = ChatClient::new(mock.clone());
        
        client.connect("ws://localhost:8080").unwrap();
        
        // Send messages
        client.send_message("Hello").unwrap();
        client.send_message("World").unwrap();
        
        // Verify sent messages
        let sent = mock.lock().unwrap().get_sent_messages();
        assert_eq!(sent.len(), 2);
        assert_eq!(sent[0], "Hello");
        assert_eq!(sent[1], "World");
    }
    
    #[test]
    fn test_message_receiving() {
        let mock = Arc::new(Mutex::new(MockWebSocket::new()));
        let client = ChatClient::new(mock.clone());
        
        client.connect("ws://localhost:8080").unwrap();
        
        // Simulate receiving messages
        let mock_guard = mock.lock().unwrap();
        mock_guard.simulate_message("Message 1".to_string());
        mock_guard.simulate_message("Message 2".to_string());
        drop(mock_guard);
        
        // Small delay to ensure callbacks execute
        std::thread::sleep(std::time::Duration::from_millis(10));
        
        // Verify received messages
        let received = client.get_received_messages();
        assert_eq!(received.len(), 2);
        assert_eq!(received[0], "Message 1");
        assert_eq!(received[1], "Message 2");
    }
    
    #[test]
    fn test_send_before_connect() {
        let mock = Arc::new(Mutex::new(MockWebSocket::new()));
        let client = ChatClient::new(mock);
        
        // Attempt to send before connecting
        let result = client.send_message("Test");
        assert!(result.is_err());
        assert_eq!(result.unwrap_err(), "Client not ready");
    }
}
```

## Testing Best Practices

### Isolation
Each test should be independent, using fresh mock instances to prevent state leakage between tests.

### Coverage Areas
- **Connection management**: Test connect, disconnect, reconnect scenarios
- **Message flow**: Verify sending and receiving in both directions
- **Error handling**: Simulate network errors, malformed messages, connection drops
- **State transitions**: Test all valid and invalid state changes
- **Edge cases**: Empty messages, very large messages, rapid sends, concurrent operations

### Async Testing Considerations
For languages with async/await, use appropriate testing frameworks that support asynchronous assertions (like `tokio::test` in Rust or Google Test with async support in C++).

### Integration vs Unit Tests
- **Unit tests**: Use mocks to test business logic in isolation
- **Integration tests**: Use actual WebSocket servers (possibly containerized) to verify end-to-end functionality
- Keep the majority of tests as fast unit tests, with a smaller suite of slower integration tests

## Summary

Unit testing WebSocket code requires abstracting network operations behind interfaces, enabling dependency injection of mock implementations. Mock WebSockets simulate connection states, message transmission, and error conditions without actual network I/O, making tests fast, reliable, and deterministic. The key patterns include defining clear interfaces (abstract classes in C++, traits in Rust), implementing comprehensive mocks with helper methods for simulating network events, injecting dependencies through constructors or factory functions, and testing state transitions explicitly. This approach isolates business logic from infrastructure, ensures comprehensive coverage of edge cases and error paths, and maintains test suite speed and reliability. Both C++ and Rust examples demonstrate how to structure testable WebSocket clients using these principles, with Rust's trait system providing compile-time polymorphism and C++'s virtual functions offering runtime flexibility.