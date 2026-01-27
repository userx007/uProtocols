# CBOR and Binary Formats: Detailed Description

## Overview

**CBOR (Concise Binary Object Representation)** is a binary data serialization format defined in RFC 8949. It's designed to be extremely compact while remaining simple to implement and extensible. CBOR is particularly valuable in WebSocket communications where bandwidth efficiency and parsing speed are critical.

## Why CBOR for WebSockets?

### Advantages over JSON:
- **Size Reduction**: 30-70% smaller payloads compared to JSON
- **Parsing Speed**: Faster encoding/decoding (binary vs text parsing)
- **Type Preservation**: Native support for binary data, dates, and precise numbers
- **No String Escaping**: Binary format eliminates escaping overhead
- **Deterministic Encoding**: Useful for cryptographic signatures

### Use Cases:
- IoT device communication with limited bandwidth
- Real-time gaming where every millisecond counts
- Financial trading systems requiring precise decimal representation
- Mobile applications on cellular networks
- Embedded systems with constrained resources

## CBOR Data Types

CBOR supports:
- **Integers**: Unsigned/signed (up to 64-bit)
- **Floating-point**: Half, single, double precision
- **Byte strings**: Raw binary data
- **Text strings**: UTF-8 encoded
- **Arrays**: Ordered sequences
- **Maps**: Key-value pairs
- **Tags**: Semantic annotations (dates, bigints, etc.)
- **Simple values**: Boolean, null, undefined

---

# C/C++ Implementation

## Using libcbor

```c
#include <cbor.h>
#include <stdio.h>
#include <string.h>

// Encoding example
void encode_cbor_message() {
    // Create a CBOR map
    cbor_item_t *root = cbor_new_definite_map(3);
    
    // Add string key-value pair
    cbor_map_add(root, (struct cbor_pair) {
        .key = cbor_move(cbor_build_string("username")),
        .value = cbor_move(cbor_build_string("alice"))
    });
    
    // Add integer key-value pair
    cbor_map_add(root, (struct cbor_pair) {
        .key = cbor_move(cbor_build_string("score")),
        .value = cbor_move(cbor_build_uint32(1500))
    });
    
    // Add boolean key-value pair
    cbor_map_add(root, (struct cbor_pair) {
        .key = cbor_move(cbor_build_string("active")),
        .value = cbor_move(cbor_build_bool(true))
    });
    
    // Serialize to buffer
    unsigned char *buffer;
    size_t buffer_size;
    size_t length = cbor_serialize_alloc(root, &buffer, &buffer_size);
    
    printf("Encoded %zu bytes\n", length);
    
    // Send buffer over WebSocket here
    // websocket_send(ws, buffer, length);
    
    free(buffer);
    cbor_decref(&root);
}

// Decoding example
void decode_cbor_message(unsigned char *data, size_t length) {
    struct cbor_load_result result;
    cbor_item_t *item = cbor_load(data, length, &result);
    
    if (result.error.code != CBOR_ERR_NONE) {
        printf("CBOR decode error\n");
        return;
    }
    
    if (cbor_isa_map(item)) {
        struct cbor_pair *pairs = cbor_map_handle(item);
        size_t size = cbor_map_size(item);
        
        for (size_t i = 0; i < size; i++) {
            // Get key as string
            if (cbor_isa_string(pairs[i].key)) {
                char *key = malloc(cbor_string_length(pairs[i].key) + 1);
                memcpy(key, cbor_string_handle(pairs[i].key), 
                       cbor_string_length(pairs[i].key));
                key[cbor_string_length(pairs[i].key)] = '\0';
                
                printf("Key: %s, ", key);
                
                // Handle different value types
                if (cbor_isa_uint(pairs[i].value)) {
                    printf("Value: %llu\n", cbor_get_uint64(pairs[i].value));
                } else if (cbor_isa_string(pairs[i].value)) {
                    printf("Value: %.*s\n", 
                           (int)cbor_string_length(pairs[i].value),
                           cbor_string_handle(pairs[i].value));
                } else if (cbor_is_bool(pairs[i].value)) {
                    printf("Value: %s\n", 
                           cbor_get_bool(pairs[i].value) ? "true" : "false");
                }
                
                free(key);
            }
        }
    }
    
    cbor_decref(&item);
}
```

## WebSocket Integration (C++)

```cpp
#include <libwebsockets.h>
#include <cbor.h>
#include <vector>

class CBORWebSocketClient {
private:
    struct lws_context *context;
    struct lws *wsi;
    std::vector<uint8_t> send_buffer;
    
public:
    void send_player_state(const std::string& player_id, 
                          float x, float y, int health) {
        // Create CBOR map
        cbor_item_t *root = cbor_new_definite_map(4);
        
        cbor_map_add(root, (struct cbor_pair) {
            .key = cbor_move(cbor_build_string("type")),
            .value = cbor_move(cbor_build_string("player_state"))
        });
        
        cbor_map_add(root, (struct cbor_pair) {
            .key = cbor_move(cbor_build_string("player_id")),
            .value = cbor_move(cbor_build_string(player_id.c_str()))
        });
        
        // Use CBOR array for position
        cbor_item_t *position = cbor_new_definite_array(2);
        cbor_array_push(position, cbor_move(cbor_build_float4(x)));
        cbor_array_push(position, cbor_move(cbor_build_float4(y)));
        
        cbor_map_add(root, (struct cbor_pair) {
            .key = cbor_move(cbor_build_string("position")),
            .value = cbor_move(position)
        });
        
        cbor_map_add(root, (struct cbor_pair) {
            .key = cbor_move(cbor_build_string("health")),
            .value = cbor_move(cbor_build_uint16(health))
        });
        
        // Serialize
        unsigned char *buffer;
        size_t buffer_size;
        size_t length = cbor_serialize_alloc(root, &buffer, &buffer_size);
        
        // Send via WebSocket with binary frame
        send_buffer.assign(buffer, buffer + length);
        lws_write(wsi, buffer, length, LWS_WRITE_BINARY);
        
        free(buffer);
        cbor_decref(&root);
    }
    
    void handle_received_data(void *in, size_t len) {
        struct cbor_load_result result;
        cbor_item_t *item = cbor_load((unsigned char*)in, len, &result);
        
        if (result.error.code == CBOR_ERR_NONE && cbor_isa_map(item)) {
            // Process received CBOR data
            // Extract fields and handle game logic
        }
        
        cbor_decref(&item);
    }
};
```

---

# Rust Implementation

## Using serde_cbor

```rust
use serde::{Serialize, Deserialize};
use serde_cbor::{to_vec, from_slice};
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};

#[derive(Serialize, Deserialize, Debug)]
struct PlayerState {
    #[serde(rename = "type")]
    msg_type: String,
    player_id: String,
    position: [f32; 2],
    health: u16,
    inventory: Vec<String>,
}

#[derive(Serialize, Deserialize, Debug)]
struct ServerMessage {
    timestamp: u64,
    event: String,
    data: serde_cbor::Value,
}

// Encoding example
fn encode_player_state() -> Vec<u8> {
    let state = PlayerState {
        msg_type: "player_state".to_string(),
        player_id: "player_123".to_string(),
        position: [45.5, 120.3],
        health: 87,
        inventory: vec![
            "sword".to_string(),
            "shield".to_string(),
            "potion".to_string(),
        ],
    };
    
    // Serialize to CBOR
    let cbor_bytes = to_vec(&state).expect("Failed to serialize");
    
    println!("Encoded {} bytes", cbor_bytes.len());
    println!("CBOR hex: {}", hex::encode(&cbor_bytes));
    
    cbor_bytes
}

// Decoding example
fn decode_player_state(data: &[u8]) -> Result<PlayerState, serde_cbor::Error> {
    let state: PlayerState = from_slice(data)?;
    println!("Decoded: {:?}", state);
    Ok(state)
}

// WebSocket client with CBOR
async fn websocket_cbor_client() -> Result<(), Box<dyn std::error::Error>> {
    let url = "ws://localhost:8080/game";
    let (ws_stream, _) = connect_async(url).await?;
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn task to send player updates
    let send_task = tokio::spawn(async move {
        loop {
            let state = PlayerState {
                msg_type: "update".to_string(),
                player_id: "alice".to_string(),
                position: [rand::random::<f32>() * 100.0, 
                          rand::random::<f32>() * 100.0],
                health: 100,
                inventory: vec!["sword".to_string()],
            };
            
            // Encode to CBOR
            let cbor_data = to_vec(&state).unwrap();
            
            // Send as binary WebSocket message
            write.send(Message::Binary(cbor_data)).await.unwrap();
            
            tokio::time::sleep(tokio::time::Duration::from_millis(50)).await;
        }
    });
    
    // Receive and decode messages
    while let Some(msg) = read.next().await {
        match msg? {
            Message::Binary(data) => {
                // Decode CBOR
                match from_slice::<ServerMessage>(&data) {
                    Ok(server_msg) => {
                        println!("Received: {:?}", server_msg);
                    }
                    Err(e) => eprintln!("Failed to decode CBOR: {}", e),
                }
            }
            Message::Close(_) => break,
            _ => {}
        }
    }
    
    send_task.abort();
    Ok(())
}
```

## Using ciborium (Alternative Rust Library)

```rust
use ciborium::{de::from_reader, ser::into_writer};
use std::collections::BTreeMap;

fn ciborium_example() {
    // Create a complex structure
    let mut game_state = BTreeMap::new();
    game_state.insert("level", ciborium::value::Value::Integer(5.into()));
    game_state.insert("score", ciborium::value::Value::Integer(12500.into()));
    game_state.insert("player", ciborium::value::Value::Text("Alice".to_string()));
    
    // Serialize to CBOR
    let mut buffer = Vec::new();
    into_writer(&game_state, &mut buffer).unwrap();
    
    println!("Encoded {} bytes", buffer.len());
    
    // Deserialize from CBOR
    let decoded: BTreeMap<String, ciborium::value::Value> = 
        from_reader(&buffer[..]).unwrap();
    
    println!("Decoded: {:?}", decoded);
}
```

## Performance Comparison Helper

```rust
use std::time::Instant;

fn compare_formats() {
    let state = PlayerState {
        msg_type: "state".to_string(),
        player_id: "player_001".to_string(),
        position: [123.45, 678.90],
        health: 95,
        inventory: vec!["item1".to_string(), "item2".to_string()],
    };
    
    // Test CBOR
    let start = Instant::now();
    for _ in 0..10000 {
        let cbor = serde_cbor::to_vec(&state).unwrap();
        let _: PlayerState = serde_cbor::from_slice(&cbor).unwrap();
    }
    let cbor_time = start.elapsed();
    
    // Test JSON
    let start = Instant::now();
    for _ in 0..10000 {
        let json = serde_json::to_string(&state).unwrap();
        let _: PlayerState = serde_json::from_str(&json).unwrap();
    }
    let json_time = start.elapsed();
    
    let cbor_size = serde_cbor::to_vec(&state).unwrap().len();
    let json_size = serde_json::to_string(&state).unwrap().len();
    
    println!("CBOR: {} bytes, {:?}", cbor_size, cbor_time);
    println!("JSON: {} bytes, {:?}", json_size, json_time);
    println!("Size reduction: {:.1}%", 
             (1.0 - cbor_size as f64 / json_size as f64) * 100.0);
}
```

---

# Summary

## Key Takeaways

**CBOR (Concise Binary Object Representation)** is a binary serialization format that significantly improves WebSocket efficiency through reduced payload sizes (30-70% smaller than JSON) and faster parsing speeds. It's particularly valuable for bandwidth-constrained scenarios like IoT devices, mobile applications, and real-time gaming.

**Implementation Considerations:**
- **C/C++**: Use `libcbor` for low-level control; ideal for embedded systems and performance-critical applications
- **Rust**: Use `serde_cbor` or `ciborium` with seamless serde integration; excellent for type-safe, high-performance systems

**When to Use CBOR:**
- Bandwidth is limited (mobile, IoT)
- Low latency is critical (gaming, trading)
- Binary data is common in payloads
- Need precise numeric types (financial data)

**When to Stick with JSON:**
- Human readability is important for debugging
- Browser-based applications without binary support
- Simple REST APIs where performance isn't critical
- Team lacks binary format experience

CBOR represents an excellent choice for optimizing WebSocket communications where efficiency matters, offering substantial performance improvements with relatively simple implementation using mature libraries across multiple languages.