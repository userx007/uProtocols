# WebSocket Transport for Modbus

## Detailed Description

WebSocket Transport for Modbus enables real-time, bidirectional communication between Modbus devices and browser-based applications or other WebSocket clients. This approach bridges the gap between traditional industrial protocols and modern web technologies, allowing web applications to interact with Modbus devices without requiring native protocol support in browsers.

### Key Concepts

**Protocol Bridging**: WebSocket acts as a tunneling mechanism, encapsulating Modbus TCP packets within WebSocket frames. This allows browsers (which don't natively support raw TCP sockets) to communicate with Modbus devices through an intermediary WebSocket-to-Modbus gateway.

**Architecture Components**:
- **WebSocket Server/Gateway**: Accepts WebSocket connections from clients and translates between WebSocket frames and Modbus TCP packets
- **Web Client**: Browser-based application using JavaScript WebSocket API
- **Modbus Device**: The target device (could be a PLC, sensor, or other industrial equipment)

**Use Cases**:
- Web-based HMI (Human-Machine Interface) dashboards
- Remote monitoring and diagnostics
- Cloud-based SCADA systems
- Mobile applications requiring Modbus connectivity

### Advantages
- Platform independence (works across operating systems and devices)
- No browser plugins required
- Real-time bidirectional communication
- Firewall-friendly (uses standard HTTP/HTTPS ports)
- Secure communication when using WSS (WebSocket Secure)

### Challenges
- Additional latency from protocol translation
- Gateway becomes a single point of failure
- Security considerations (authentication, authorization)
- Message framing overhead

---

## C/C++ Implementation

### WebSocket-to-Modbus Gateway (Server-side)

```cpp
#include <libwebsockets.h>
#include <modbus/modbus.h>
#include <string.h>
#include <stdlib.h>

#define MAX_PAYLOAD 512

// Context structure to hold per-session data
struct session_data {
    modbus_t *mb_ctx;
    unsigned char buffer[MAX_PAYLOAD];
    size_t buffer_len;
};

// WebSocket callback handler
static int callback_modbus_ws(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("WebSocket connection established\n");
            // Initialize Modbus TCP connection
            session->mb_ctx = modbus_new_tcp("192.168.1.100", 502);
            if (session->mb_ctx == NULL) {
                fprintf(stderr, "Failed to create Modbus context\n");
                return -1;
            }
            if (modbus_connect(session->mb_ctx) == -1) {
                fprintf(stderr, "Modbus connection failed: %s\n", 
                        modbus_strerror(errno));
                modbus_free(session->mb_ctx);
                return -1;
            }
            break;
            
        case LWS_CALLBACK_RECEIVE:
            printf("Received %zu bytes from WebSocket\n", len);
            
            // Copy received data (Modbus request from browser)
            if (len > MAX_PAYLOAD) {
                fprintf(stderr, "Payload too large\n");
                return -1;
            }
            
            memcpy(session->buffer, in, len);
            session->buffer_len = len;
            
            // Process Modbus request
            uint16_t registers[125];
            int rc;
            
            // Example: Read holding registers
            // Assuming first byte indicates function code
            uint8_t function = session->buffer[0];
            uint16_t addr = (session->buffer[1] << 8) | session->buffer[2];
            uint16_t count = (session->buffer[3] << 8) | session->buffer[4];
            
            if (function == 0x03) { // Read Holding Registers
                rc = modbus_read_registers(session->mb_ctx, addr, count, registers);
                if (rc == -1) {
                    fprintf(stderr, "Modbus read failed: %s\n", 
                            modbus_strerror(errno));
                    return -1;
                }
                
                // Prepare response
                unsigned char response[LWS_PRE + MAX_PAYLOAD];
                size_t response_len = 1 + (rc * 2); // Function code + data
                response[LWS_PRE] = function;
                
                for (int i = 0; i < rc; i++) {
                    response[LWS_PRE + 1 + (i * 2)] = registers[i] >> 8;
                    response[LWS_PRE + 2 + (i * 2)] = registers[i] & 0xFF;
                }
                
                // Send response back via WebSocket
                lws_write(wsi, &response[LWS_PRE], response_len, LWS_WRITE_BINARY);
            }
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("WebSocket connection closed\n");
            if (session->mb_ctx) {
                modbus_close(session->mb_ctx);
                modbus_free(session->mb_ctx);
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "modbus-protocol",
        callback_modbus_ws,
        sizeof(struct session_data),
        MAX_PAYLOAD,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create WebSocket context\n");
        return -1;
    }
    
    printf("WebSocket-to-Modbus gateway running on port 8080\n");
    
    // Event loop
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

---

## Rust Implementation

### WebSocket-to-Modbus Gateway

```rust
use tokio::net::TcpStream;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use tokio_modbus::prelude::*;
use std::net::SocketAddr;
use tokio::net::TcpListener;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:8080".parse::<SocketAddr>()?;
    let listener = TcpListener::bind(&addr).await?;
    println!("WebSocket-to-Modbus gateway listening on: {}", addr);

    while let Ok((stream, _)) = listener.accept().await {
        tokio::spawn(handle_connection(stream));
    }

    Ok(())
}

async fn handle_connection(stream: TcpStream) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    println!("New WebSocket connection established");

    let (mut ws_sender, mut ws_receiver) = ws_stream.split();

    // Connect to Modbus TCP device
    let socket_addr = "192.168.1.100:502".parse()?;
    let mut modbus_ctx = tcp::connect(socket_addr).await?;

    while let Some(msg) = ws_receiver.next().await {
        match msg {
            Ok(Message::Binary(data)) => {
                println!("Received {} bytes from WebSocket", data.len());

                // Parse Modbus request from WebSocket data
                if data.len() < 5 {
                    eprintln!("Invalid Modbus request length");
                    continue;
                }

                let function_code = data[0];
                let address = u16::from_be_bytes([data[1], data[2]]);
                let count = u16::from_be_bytes([data[3], data[4]]);

                // Process based on function code
                let response = match function_code {
                    0x03 => {
                        // Read Holding Registers
                        match modbus_ctx.read_holding_registers(address, count).await {
                            Ok(registers) => {
                                let mut resp = vec![function_code];
                                for reg in registers {
                                    resp.extend_from_slice(&reg.to_be_bytes());
                                }
                                resp
                            }
                            Err(e) => {
                                eprintln!("Modbus read error: {}", e);
                                vec![function_code | 0x80, 0x04] // Exception response
                            }
                        }
                    }
                    0x06 => {
                        // Write Single Register
                        if data.len() >= 7 {
                            let value = u16::from_be_bytes([data[5], data[6]]);
                            match modbus_ctx.write_single_register(address, value).await {
                                Ok(_) => {
                                    data[0..7].to_vec() // Echo request as response
                                }
                                Err(e) => {
                                    eprintln!("Modbus write error: {}", e);
                                    vec![function_code | 0x80, 0x04]
                                }
                            }
                        } else {
                            vec![function_code | 0x80, 0x03] // Invalid data length
                        }
                    }
                    0x10 => {
                        // Write Multiple Registers
                        if data.len() >= 7 {
                            let byte_count = data[6] as usize;
                            if data.len() >= 7 + byte_count {
                                let mut values = Vec::new();
                                for i in (0..byte_count).step_by(2) {
                                    values.push(u16::from_be_bytes([
                                        data[7 + i],
                                        data[7 + i + 1]
                                    ]));
                                }
                                
                                match modbus_ctx.write_multiple_registers(address, &values).await {
                                    Ok(_) => data[0..6].to_vec(),
                                    Err(e) => {
                                        eprintln!("Modbus write error: {}", e);
                                        vec![function_code | 0x80, 0x04]
                                    }
                                }
                            } else {
                                vec![function_code | 0x80, 0x03]
                            }
                        } else {
                            vec![function_code | 0x80, 0x03]
                        }
                    }
                    _ => {
                        eprintln!("Unsupported function code: {}", function_code);
                        vec![function_code | 0x80, 0x01] // Illegal function
                    }
                };

                // Send response back via WebSocket
                ws_sender.send(Message::Binary(response)).await?;
            }
            Ok(Message::Close(_)) => {
                println!("WebSocket connection closing");
                break;
            }
            Ok(_) => {} // Ignore other message types
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
        }
    }

    Ok(())
}
```

### Browser Client Example (JavaScript)

```javascript
// WebSocket Modbus Client (browser-side)
class ModbusWebSocketClient {
    constructor(url) {
        this.ws = new WebSocket(url);
        this.ws.binaryType = 'arraybuffer';
        
        this.ws.onopen = () => {
            console.log('Connected to Modbus gateway');
        };
        
        this.ws.onmessage = (event) => {
            this.handleResponse(new Uint8Array(event.data));
        };
        
        this.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };
    }
    
    readHoldingRegisters(address, count) {
        const request = new Uint8Array([
            0x03,                    // Function code
            (address >> 8) & 0xFF,   // Address high byte
            address & 0xFF,          // Address low byte
            (count >> 8) & 0xFF,     // Count high byte
            count & 0xFF             // Count low byte
        ]);
        
        this.ws.send(request);
    }
    
    writeSingleRegister(address, value) {
        const request = new Uint8Array([
            0x06,
            (address >> 8) & 0xFF,
            address & 0xFF,
            (value >> 8) & 0xFF,
            value & 0xFF
        ]);
        
        this.ws.send(request);
    }
    
    handleResponse(data) {
        const functionCode = data[0];
        
        if (functionCode & 0x80) {
            console.error('Modbus exception:', data[1]);
            return;
        }
        
        if (functionCode === 0x03) {
            const registers = [];
            for (let i = 1; i < data.length; i += 2) {
                registers.push((data[i] << 8) | data[i + 1]);
            }
            console.log('Read registers:', registers);
        }
    }
}

// Usage
const client = new ModbusWebSocketClient('ws://localhost:8080');
client.readHoldingRegisters(100, 5);
```

---

## Summary

**WebSocket Transport for Modbus** enables browser-based and web applications to communicate with Modbus devices through a gateway that translates between WebSocket and Modbus TCP protocols. This solution provides platform-independent, real-time access to industrial devices without requiring browser plugins or native protocol support.

**Key Points**:
- **Gateway Architecture**: A server component bridges WebSocket clients and Modbus devices
- **Binary Framing**: Modbus requests/responses are transmitted as binary WebSocket messages
- **Function Code Routing**: The gateway parses function codes and routes requests appropriately
- **Security**: Can be enhanced with WSS (WebSocket Secure) and authentication mechanisms
- **Performance Considerations**: Additional latency from protocol translation and WebSocket overhead

**Implementation Highlights**:
- **C/C++**: Uses libwebsockets and libmodbus libraries for efficient gateway implementation
- **Rust**: Leverages tokio-tungstenite and tokio-modbus for async, safe WebSocket-Modbus bridging
- **Client-side**: Standard JavaScript WebSocket API enables any browser to become a Modbus client

This approach is particularly valuable for modern web-based HMI systems, cloud SCADA platforms, and IoT dashboards that need to interact with traditional industrial protocols.