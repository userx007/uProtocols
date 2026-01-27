# WebSocket Integration in React Applications

## Detailed Description

React Integration for WebSockets involves managing real-time, bidirectional communication channels within React applications using modern React patterns like hooks and context. This approach enables components to establish, maintain, and share WebSocket connections efficiently while adhering to React's component lifecycle and state management principles.

### Key Concepts

**WebSocket Lifecycle in React:**
- Connection establishment typically occurs in `useEffect` hooks
- Cleanup functions ensure proper disconnection when components unmount
- Connection state needs to be managed reactively to trigger UI updates

**State Management Challenges:**
- Multiple components may need access to the same WebSocket connection
- Message handling requires updating component state reactively
- Connection status (connecting, open, closed, error) must be tracked
- Reconnection logic needs to be implemented for reliability

**React Patterns for WebSockets:**
1. **Custom Hooks**: Encapsulate WebSocket logic for reusability
2. **Context API**: Share connections across component tree
3. **useReducer**: Manage complex WebSocket state transitions
4. **useRef**: Store WebSocket instances without triggering re-renders

## Code Examples

### React with JavaScript/TypeScript

#### 1. Basic Custom Hook
```typescript
// useWebSocket.ts
import { useEffect, useRef, useState } from 'react';

interface UseWebSocketOptions {
  onOpen?: (event: Event) => void;
  onMessage?: (event: MessageEvent) => void;
  onError?: (event: Event) => void;
  onClose?: (event: CloseEvent) => void;
  reconnect?: boolean;
  reconnectInterval?: number;
}

export const useWebSocket = (url: string, options: UseWebSocketOptions = {}) => {
  const [isConnected, setIsConnected] = useState(false);
  const [lastMessage, setLastMessage] = useState<MessageEvent | null>(null);
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimeoutRef = useRef<NodeJS.Timeout>();
  const shouldReconnect = useRef(options.reconnect ?? true);

  const connect = () => {
    try {
      const ws = new WebSocket(url);
      
      ws.onopen = (event) => {
        setIsConnected(true);
        options.onOpen?.(event);
      };

      ws.onmessage = (event) => {
        setLastMessage(event);
        options.onMessage?.(event);
      };

      ws.onerror = (event) => {
        options.onError?.(event);
      };

      ws.onclose = (event) => {
        setIsConnected(false);
        wsRef.current = null;
        options.onClose?.(event);

        // Reconnection logic
        if (shouldReconnect.current) {
          reconnectTimeoutRef.current = setTimeout(() => {
            connect();
          }, options.reconnectInterval ?? 3000);
        }
      };

      wsRef.current = ws;
    } catch (error) {
      console.error('WebSocket connection error:', error);
    }
  };

  const sendMessage = (data: string | ArrayBuffer | Blob) => {
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(data);
    } else {
      console.warn('WebSocket is not connected');
    }
  };

  const disconnect = () => {
    shouldReconnect.current = false;
    if (reconnectTimeoutRef.current) {
      clearTimeout(reconnectTimeoutRef.current);
    }
    if (wsRef.current) {
      wsRef.current.close();
    }
  };

  useEffect(() => {
    connect();

    return () => {
      shouldReconnect.current = false;
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, [url]);

  return {
    isConnected,
    lastMessage,
    sendMessage,
    disconnect,
  };
};
```

#### 2. WebSocket Context Provider
```typescript
// WebSocketContext.tsx
import React, { createContext, useContext, useEffect, useRef, useState } from 'react';

interface WebSocketContextType {
  isConnected: boolean;
  sendMessage: (message: any) => void;
  subscribe: (handler: (data: any) => void) => () => void;
}

const WebSocketContext = createContext<WebSocketContextType | undefined>(undefined);

export const WebSocketProvider: React.FC<{ url: string; children: React.ReactNode }> = ({ 
  url, 
  children 
}) => {
  const [isConnected, setIsConnected] = useState(false);
  const wsRef = useRef<WebSocket | null>(null);
  const subscribersRef = useRef<Set<(data: any) => void>>(new Set());

  useEffect(() => {
    const ws = new WebSocket(url);

    ws.onopen = () => {
      setIsConnected(true);
      console.log('WebSocket connected');
    };

    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        // Notify all subscribers
        subscribersRef.current.forEach(handler => handler(data));
      } catch (error) {
        console.error('Error parsing message:', error);
      }
    };

    ws.onerror = (error) => {
      console.error('WebSocket error:', error);
    };

    ws.onclose = () => {
      setIsConnected(false);
      console.log('WebSocket disconnected');
    };

    wsRef.current = ws;

    return () => {
      ws.close();
    };
  }, [url]);

  const sendMessage = (message: any) => {
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(message));
    }
  };

  const subscribe = (handler: (data: any) => void) => {
    subscribersRef.current.add(handler);
    // Return unsubscribe function
    return () => {
      subscribersRef.current.delete(handler);
    };
  };

  return (
    <WebSocketContext.Provider value={{ isConnected, sendMessage, subscribe }}>
      {children}
    </WebSocketContext.Provider>
  );
};

export const useWebSocketContext = () => {
  const context = useContext(WebSocketContext);
  if (!context) {
    throw new Error('useWebSocketContext must be used within WebSocketProvider');
  }
  return context;
};
```

#### 3. Component Using Context
```typescript
// ChatComponent.tsx
import React, { useState, useEffect } from 'react';
import { useWebSocketContext } from './WebSocketContext';

interface Message {
  id: string;
  text: string;
  sender: string;
  timestamp: number;
}

export const ChatComponent: React.FC = () => {
  const { isConnected, sendMessage, subscribe } = useWebSocketContext();
  const [messages, setMessages] = useState<Message[]>([]);
  const [inputValue, setInputValue] = useState('');

  useEffect(() => {
    // Subscribe to messages
    const unsubscribe = subscribe((data: Message) => {
      if (data.text) {
        setMessages(prev => [...prev, data]);
      }
    });

    // Cleanup subscription
    return unsubscribe;
  }, [subscribe]);

  const handleSend = () => {
    if (inputValue.trim() && isConnected) {
      const message: Message = {
        id: Date.now().toString(),
        text: inputValue,
        sender: 'me',
        timestamp: Date.now(),
      };
      sendMessage(message);
      setInputValue('');
    }
  };

  return (
    <div>
      <div>Status: {isConnected ? 'Connected' : 'Disconnected'}</div>
      <div style={{ height: '400px', overflow: 'auto', border: '1px solid #ccc' }}>
        {messages.map(msg => (
          <div key={msg.id}>
            <strong>{msg.sender}:</strong> {msg.text}
          </div>
        ))}
      </div>
      <input 
        value={inputValue} 
        onChange={(e) => setInputValue(e.target.value)}
        onKeyPress={(e) => e.key === 'Enter' && handleSend()}
      />
      <button onClick={handleSend} disabled={!isConnected}>Send</button>
    </div>
  );
};
```

### C/C++ WebSocket Client (for backend services)

```cpp
// websocket_client.cpp
#include <iostream>
#include <string>
#include <thread>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebSocketClient {
private:
    net::io_context ioc_;
    tcp::resolver resolver_;
    websocket::stream<tcp::socket> ws_;
    std::string host_;
    std::string port_;
    bool connected_;

public:
    WebSocketClient(const std::string& host, const std::string& port)
        : resolver_(ioc_), ws_(ioc_), host_(host), port_(port), connected_(false) {}

    void connect() {
        try {
            // Resolve the host
            auto const results = resolver_.resolve(host_, port_);
            
            // Connect to the IP address
            net::connect(ws_.next_layer(), results.begin(), results.end());
            
            // Set WebSocket options
            ws_.set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(http::field::user_agent, "WebSocket-Client/1.0");
                }
            ));
            
            // Perform WebSocket handshake
            ws_.handshake(host_, "/");
            connected_ = true;
            
            std::cout << "WebSocket connected to " << host_ << ":" << port_ << std::endl;
        } catch (std::exception const& e) {
            std::cerr << "Connection error: " << e.what() << std::endl;
            connected_ = false;
        }
    }

    void send(const std::string& message) {
        if (!connected_) {
            std::cerr << "Not connected" << std::endl;
            return;
        }

        try {
            ws_.write(net::buffer(message));
            std::cout << "Sent: " << message << std::endl;
        } catch (std::exception const& e) {
            std::cerr << "Send error: " << e.what() << std::endl;
        }
    }

    std::string receive() {
        if (!connected_) {
            return "";
        }

        try {
            beast::flat_buffer buffer;
            ws_.read(buffer);
            
            std::string message = beast::buffers_to_string(buffer.data());
            std::cout << "Received: " << message << std::endl;
            return message;
        } catch (std::exception const& e) {
            std::cerr << "Receive error: " << e.what() << std::endl;
            return "";
        }
    }

    void close() {
        if (connected_) {
            try {
                ws_.close(websocket::close_code::normal);
                connected_ = false;
                std::cout << "WebSocket closed" << std::endl;
            } catch (std::exception const& e) {
                std::cerr << "Close error: " << e.what() << std::endl;
            }
        }
    }

    bool is_connected() const { return connected_; }

    ~WebSocketClient() {
        close();
    }
};

// Example usage
int main() {
    WebSocketClient client("echo.websocket.org", "80");
    
    client.connect();
    
    if (client.is_connected()) {
        client.send("Hello, WebSocket!");
        
        // Receive in a loop
        for (int i = 0; i < 5; i++) {
            std::string msg = client.receive();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        client.close();
    }
    
    return 0;
}
```

### Rust WebSocket Client

```rust
// Cargo.toml dependencies:
// tokio = { version = "1.0", features = ["full"] }
// tokio-tungstenite = "0.20"
// futures-util = "0.3"

use futures_util::{SinkExt, StreamExt};
use tokio::net::TcpStream;
use tokio_tungstenite::{connect_async, tungstenite::Message, MaybeTlsStream, WebSocketStream};
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct WebSocketClient {
    url: String,
    ws_stream: Option<Arc<Mutex<WebSocketStream<MaybeTlsStream<TcpStream>>>>>,
}

impl WebSocketClient {
    pub fn new(url: String) -> Self {
        WebSocketClient {
            url,
            ws_stream: None,
        }
    }

    pub async fn connect(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        let (ws_stream, _) = connect_async(&self.url).await?;
        println!("WebSocket connected to {}", self.url);
        
        self.ws_stream = Some(Arc::new(Mutex::new(ws_stream)));
        Ok(())
    }

    pub async fn send(&self, message: &str) -> Result<(), Box<dyn std::error::Error>> {
        if let Some(ws) = &self.ws_stream {
            let mut ws_lock = ws.lock().await;
            ws_lock.send(Message::Text(message.to_string())).await?;
            println!("Sent: {}", message);
            Ok(())
        } else {
            Err("Not connected".into())
        }
    }

    pub async fn receive(&self) -> Result<Option<String>, Box<dyn std::error::Error>> {
        if let Some(ws) = &self.ws_stream {
            let mut ws_lock = ws.lock().await;
            if let Some(msg) = ws_lock.next().await {
                match msg? {
                    Message::Text(text) => {
                        println!("Received: {}", text);
                        Ok(Some(text))
                    }
                    Message::Binary(data) => {
                        println!("Received binary data: {} bytes", data.len());
                        Ok(Some(format!("Binary({} bytes)", data.len())))
                    }
                    Message::Close(_) => {
                        println!("Connection closed by server");
                        Ok(None)
                    }
                    _ => Ok(None),
                }
            } else {
                Ok(None)
            }
        } else {
            Err("Not connected".into())
        }
    }

    pub async fn close(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if let Some(ws) = self.ws_stream.take() {
            let mut ws_lock = ws.lock().await;
            ws_lock.close(None).await?;
            println!("WebSocket closed");
        }
        Ok(())
    }
}

// React-like hook pattern in Rust (conceptual)
pub struct WebSocketHook {
    client: Arc<Mutex<WebSocketClient>>,
    connected: Arc<Mutex<bool>>,
}

impl WebSocketHook {
    pub async fn new(url: String) -> Self {
        let mut client = WebSocketClient::new(url);
        let connected = Arc::new(Mutex::new(false));
        
        if client.connect().await.is_ok() {
            *connected.lock().await = true;
        }
        
        WebSocketHook {
            client: Arc::new(Mutex::new(client)),
            connected,
        }
    }

    pub async fn send_message(&self, message: &str) -> Result<(), Box<dyn std::error::Error>> {
        let client = self.client.lock().await;
        client.send(message).await
    }

    pub async fn is_connected(&self) -> bool {
        *self.connected.lock().await
    }
}

// Example usage
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = WebSocketClient::new("ws://echo.websocket.org".to_string());
    
    client.connect().await?;
    
    // Send a message
    client.send("Hello from Rust!").await?;
    
    // Receive messages
    for _ in 0..5 {
        if let Some(msg) = client.receive().await? {
            println!("Got message: {}", msg);
        }
        tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
    }
    
    client.close().await?;
    
    Ok(())
}
```

## Summary

WebSocket integration in React applications requires careful management of connection lifecycle, state, and component communication. The custom hook pattern (`useWebSocket`) provides a clean, reusable way to encapsulate WebSocket logic with automatic cleanup and reconnection capabilities. For applications where multiple components need access to the same connection, the Context API pattern creates a centralized connection manager that prevents duplicate connections and enables efficient message broadcasting through a subscriber pattern.

Key implementation considerations include proper cleanup in `useEffect` return functions to prevent memory leaks, using `useRef` to store WebSocket instances without triggering unnecessary re-renders, implementing reconnection logic for resilience, and managing connection state reactively to update the UI appropriately. The C++ and Rust examples demonstrate how backend services can implement WebSocket clients using libraries like Boost.Beast and tokio-tungstenite, showing the full-stack nature of WebSocket communication patterns. These patterns enable real-time features like live chat, collaborative editing, real-time dashboards, and push notifications in modern web applications.