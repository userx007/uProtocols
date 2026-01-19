# Integration Testing for WebSocket Applications

## Overview

Integration testing for WebSocket applications focuses on validating the complete client-server communication flow, ensuring that all components work together correctly in real-world scenarios. Unlike unit tests that examine individual functions, integration tests verify the entire WebSocket lifecycle: connection establishment, message exchange, error handling, reconnection logic, and graceful shutdown.

## Key Testing Strategies

### 1. **Connection Lifecycle Testing**
- Verify successful handshake completion
- Test connection upgrades from HTTP to WebSocket
- Validate protocol negotiation and subprotocol selection
- Ensure proper handling of connection timeouts

### 2. **Message Exchange Testing**
- Validate bidirectional communication (client ↔ server)
- Test different message types (text, binary, ping/pong)
- Verify message ordering and delivery guarantees
- Test fragmented message handling
- Validate large payload transmission

### 3. **Error Handling & Recovery**
- Test network interruptions and reconnection logic
- Verify graceful degradation under adverse conditions
- Test timeout scenarios (idle connections, slow responses)
- Validate error code propagation (close codes 1000-1015)

### 4. **Concurrent Connection Testing**
- Test multiple simultaneous client connections
- Verify server scalability and resource management
- Test broadcast scenarios (server → multiple clients)

### 5. **Security Testing**
- Validate TLS/SSL connections (wss://)
- Test authentication and authorization flows
- Verify protection against common attacks (message flooding, malformed frames)

---

## Code Examples

### C/C++ Integration Test Example

Using **libwebsockets** library with a simple test framework:

```cpp
#include <libwebsockets.h>
#include <string.h>
#include <assert.h>
#include <thread>
#include <chrono>

// Test state tracker
struct TestContext {
    bool connected = false;
    bool message_received = false;
    bool connection_closed = false;
    std::string received_message;
};

// WebSocket callback for client
static int client_callback(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
    TestContext *ctx = (TestContext *)lws_context_user(lws_get_context(wsi));
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            ctx->connected = true;
            printf("[TEST] Connection established\n");
            // Send test message
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            ctx->message_received = true;
            ctx->received_message = std::string((char *)in, len);
            printf("[TEST] Received: %s\n", ctx->received_message.c_str());
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            const char *msg = "Hello from integration test";
            unsigned char buf[LWS_PRE + 256];
            memcpy(&buf[LWS_PRE], msg, strlen(msg));
            lws_write(wsi, &buf[LWS_PRE], strlen(msg), LWS_WRITE_TEXT);
            break;
        }
        
        case LWS_CALLBACK_CLOSED:
            ctx->connection_closed = true;
            printf("[TEST] Connection closed\n");
            break;
            
        default:
            break;
    }
    return 0;
}

// Integration test function
bool run_integration_test() {
    TestContext test_ctx;
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    static struct lws_protocols protocols[] = {
        { "test-protocol", client_callback, 0, 1024, },
        { NULL, NULL, 0, 0 }
    };
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = &test_ctx;
    
    struct lws_context *context = lws_create_context(&info);
    assert(context != NULL);
    
    // Connect to test server
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = context;
    ccinfo.address = "localhost";
    ccinfo.port = 8080;
    ccinfo.path = "/";
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = "test-protocol";
    
    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    assert(wsi != NULL);
    
    // Run event loop for max 5 seconds
    int timeout = 50; // 5 seconds (50 * 100ms)
    while (timeout-- > 0 && !test_ctx.connection_closed) {
        lws_service(context, 100);
    }
    
    lws_context_destroy(context);
    
    // Verify test assertions
    assert(test_ctx.connected == true);
    assert(test_ctx.message_received == true);
    assert(test_ctx.received_message == "Echo: Hello from integration test");
    
    printf("[TEST] ✓ All assertions passed\n");
    return true;
}

int main() {
    printf("Starting WebSocket Integration Tests\n");
    printf("=====================================\n");
    
    if (run_integration_test()) {
        printf("\n✓ Integration test suite PASSED\n");
        return 0;
    } else {
        printf("\n✗ Integration test suite FAILED\n");
        return 1;
    }
}
```

---

### Rust Integration Test Example

Using **tokio-tungstenite** with **tokio::test** framework:

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use std::time::Duration;
use tokio::time::timeout;

#[cfg(test)]
mod integration_tests {
    use super::*;

    // Helper function to start a test WebSocket server
    async fn start_test_server() -> tokio::task::JoinHandle<()> {
        tokio::spawn(async {
            use tokio::net::TcpListener;
            use tokio_tungstenite::accept_async;
            
            let listener = TcpListener::bind("127.0.0.1:9001")
                .await
                .expect("Failed to bind");
            
            while let Ok((stream, _)) = listener.accept().await {
                tokio::spawn(async move {
                    let mut ws = accept_async(stream)
                        .await
                        .expect("Failed to accept WebSocket");
                    
                    while let Some(msg) = ws.next().await {
                        match msg {
                            Ok(Message::Text(text)) => {
                                // Echo with prefix
                                let response = format!("Echo: {}", text);
                                ws.send(Message::Text(response)).await.ok();
                            }
                            Ok(Message::Close(_)) => break,
                            _ => {}
                        }
                    }
                });
            }
        })
    }

    #[tokio::test]
    async fn test_connection_establishment() {
        let _server = start_test_server().await;
        tokio::time::sleep(Duration::from_millis(100)).await;
        
        let result = timeout(
            Duration::from_secs(5),
            connect_async("ws://127.0.0.1:9001")
        ).await;
        
        assert!(result.is_ok(), "Connection should succeed");
        let (ws_stream, _) = result.unwrap().unwrap();
        
        println!("✓ Connection established successfully");
    }

    #[tokio::test]
    async fn test_bidirectional_message_exchange() {
        let _server = start_test_server().await;
        tokio::time::sleep(Duration::from_millis(100)).await;
        
        let (ws_stream, _) = connect_async("ws://127.0.0.1:9001")
            .await
            .expect("Failed to connect");
        
        let (mut write, mut read) = ws_stream.split();
        
        // Send test message
        let test_message = "Integration test message";
        write.send(Message::Text(test_message.to_string()))
            .await
            .expect("Failed to send message");
        
        // Receive echo response
        let response = timeout(Duration::from_secs(2), read.next())
            .await
            .expect("Timeout waiting for response")
            .expect("No message received")
            .expect("Message error");
        
        if let Message::Text(text) = response {
            assert_eq!(text, format!("Echo: {}", test_message));
            println!("✓ Bidirectional communication verified");
        } else {
            panic!("Expected text message");
        }
    }

    #[tokio::test]
    async fn test_multiple_message_sequence() {
        let _server = start_test_server().await;
        tokio::time::sleep(Duration::from_millis(100)).await;
        
        let (ws_stream, _) = connect_async("ws://127.0.0.1:9001")
            .await
            .expect("Failed to connect");
        
        let (mut write, mut read) = ws_stream.split();
        
        let messages = vec!["Message 1", "Message 2", "Message 3"];
        
        for msg in &messages {
            write.send(Message::Text(msg.to_string()))
                .await
                .expect("Failed to send");
            
            let response = timeout(Duration::from_secs(2), read.next())
                .await
                .expect("Timeout")
                .expect("No response")
                .expect("Error");
            
            if let Message::Text(text) = response {
                assert_eq!(text, format!("Echo: {}", msg));
            }
        }
        
        println!("✓ Sequential message exchange verified");
    }

    #[tokio::test]
    async fn test_graceful_connection_close() {
        let _server = start_test_server().await;
        tokio::time::sleep(Duration::from_millis(100)).await;
        
        let (ws_stream, _) = connect_async("ws://127.0.0.1:9001")
            .await
            .expect("Failed to connect");
        
        let (mut write, _read) = ws_stream.split();
        
        // Send close frame
        write.send(Message::Close(None))
            .await
            .expect("Failed to close");
        
        println!("✓ Graceful close completed");
    }

    #[tokio::test]
    async fn test_connection_timeout() {
        // Attempt to connect to non-existent server
        let result = timeout(
            Duration::from_secs(2),
            connect_async("ws://127.0.0.1:9999")
        ).await;
        
        // Should either timeout or fail to connect
        assert!(
            result.is_err() || result.unwrap().is_err(),
            "Should fail to connect to invalid endpoint"
        );
        
        println!("✓ Connection timeout handling verified");
    }

    #[tokio::test]
    async fn test_binary_message_transfer() {
        let _server = start_test_server().await;
        tokio::time::sleep(Duration::from_millis(100)).await;
        
        let (ws_stream, _) = connect_async("ws://127.0.0.1:9001")
            .await
            .expect("Failed to connect");
        
        let (mut write, mut read) = ws_stream.split();
        
        // Send binary data
        let binary_data = vec![0x01, 0x02, 0x03, 0x04, 0xFF];
        write.send(Message::Binary(binary_data.clone()))
            .await
            .expect("Failed to send binary");
        
        // Note: This test assumes server echoes binary (implementation dependent)
        println!("✓ Binary message transmission completed");
    }
}
```

---

### Advanced C++ Integration Test with Mock Server

```cpp
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>
#include <thread>
#include <future>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef websocketpp::client<websocketpp::config::asio> client;

class IntegrationTestSuite {
private:
    server test_server;
    std::thread server_thread;
    uint16_t port = 9002;
    
public:
    void setup_mock_server() {
        test_server.set_reuse_addr(true);
        
        test_server.set_message_handler([](websocketpp::connection_hdl hdl, 
                                          server::message_ptr msg) {
            auto srv = (server*)msg->get_payload().c_str(); // Access server
            srv->send(hdl, "Echo: " + msg->get_payload(), msg->get_opcode());
        });
        
        test_server.init_asio();
        test_server.listen(port);
        test_server.start_accept();
        
        server_thread = std::thread([this]() {
            test_server.run();
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void teardown() {
        test_server.stop_listening();
        test_server.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
    
    bool test_round_trip_latency() {
        client c;
        c.init_asio();
        
        std::promise<bool> test_complete;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        c.set_message_handler([&](websocketpp::connection_hdl, 
                                  client::message_ptr msg) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();
            
            std::cout << "Round-trip latency: " << latency << "ms\n";
            test_complete.set_value(msg->get_payload() == "Echo: latency_test");
        });
        
        websocketpp::lib::error_code ec;
        auto con = c.get_connection("ws://localhost:" + std::to_string(port), ec);
        c.connect(con);
        
        std::thread([&c]() { c.run(); }).detach();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        con->send("latency_test");
        
        return test_complete.get_future().get();
    }
};
```

---

## Summary

**Integration testing for WebSocket applications** is essential for ensuring reliable real-time communication systems. Key takeaways:

1. **Comprehensive Coverage**: Test the complete lifecycle—connection, messaging, errors, and teardown—not just individual components.

2. **Realistic Scenarios**: Use actual network conditions, test concurrent connections, and simulate failure scenarios like network drops or server unavailability.

3. **Language-Specific Tools**:
   - **C/C++**: Use libraries like `libwebsockets` or `websocketpp` with custom test harnesses
   - **Rust**: Leverage `tokio-tungstenite` with `tokio::test` for async testing with excellent timeout and concurrency support

4. **Critical Test Areas**:
   - Connection establishment and handshake validation
   - Bidirectional message flow (text and binary)
   - Error handling and graceful degradation
   - Performance metrics (latency, throughput)
   - Security (TLS, authentication)

5. **Best Practices**:
   - Use mock/test servers for controlled environments
   - Implement timeouts to prevent hanging tests
   - Test both success and failure paths
   - Validate message ordering and delivery
   - Include load/stress tests for production readiness

Integration tests bridge the gap between unit tests and production deployment, catching issues that only appear when components interact. For WebSocket applications, where real-time behavior and connection stability are paramount, robust integration testing is not optional—it's fundamental to delivering reliable services.