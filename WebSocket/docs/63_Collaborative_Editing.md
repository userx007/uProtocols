# Collaborative Editing with WebSockets

## Detailed Description

Collaborative editing enables multiple users to simultaneously edit the same document in real-time, with changes from each user instantly visible to all others. This technology powers applications like Google Docs, Figma, and collaborative code editors.

The core challenge in collaborative editing is **conflict resolution**: when multiple users edit the same document simultaneously, their changes must be merged in a way that preserves intent and maintains consistency across all clients.

### Key Concepts

**Operational Transformation (OT)** is a technique that transforms operations based on concurrent operations that have already been applied. When two users edit simultaneously, OT adjusts each operation so they can be applied in any order while achieving the same final state.

**Conflict-Free Replicated Data Types (CRDTs)** are data structures designed to be replicated across multiple nodes, where updates can be applied in any order and all replicas will eventually converge to the same state without explicit conflict resolution.

### Core Components

1. **Document State Management**: Each client maintains a local copy of the document and tracks its version/state
2. **Operation Generation**: User edits generate operations (insert, delete, format)
3. **Operation Transmission**: Operations are sent via WebSocket to the server and other clients
4. **Transformation/Merging**: Incoming operations are transformed or merged with local state
5. **Acknowledgment System**: Ensures operations are received and applied correctly

### Architecture Patterns

- **Client-Server Model**: Server acts as central authority, clients send operations to server which broadcasts to others
- **Peer-to-Peer**: Clients communicate directly, more complex but eliminates single point of failure
- **Hybrid**: Server coordinates but clients can have direct channels for low-latency updates

## C/C++ Implementation

Here's a WebSocket-based collaborative editor using Operational Transformation:

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <libwebsockets.h>
#include <json/json.h>
#include <mutex>

// Operation types
enum OperationType {
    INSERT,
    DELETE,
    RETAIN
};

// Represents a single edit operation
struct Operation {
    OperationType type;
    int position;
    std::string content;
    int length;
    std::string clientId;
    int revision;
    
    Operation(OperationType t, int pos, const std::string& cont = "", int len = 0)
        : type(t), position(pos), content(cont), length(len), revision(0) {}
};

// Operational Transformation engine
class OTEngine {
private:
    std::string document;
    int currentRevision;
    std::mutex docMutex;
    
public:
    OTEngine() : document(""), currentRevision(0) {}
    
    // Transform operation against another operation
    Operation transform(const Operation& op1, const Operation& op2) {
        Operation transformed = op1;
        
        if (op2.type == INSERT) {
            if (op2.position <= op1.position) {
                transformed.position += op2.content.length();
            }
        } else if (op2.type == DELETE) {
            if (op2.position < op1.position) {
                transformed.position -= std::min(op2.length, op1.position - op2.position);
            }
        }
        
        return transformed;
    }
    
    // Apply operation to document
    bool applyOperation(Operation& op) {
        std::lock_guard<std::mutex> lock(docMutex);
        
        try {
            switch (op.type) {
                case INSERT:
                    if (op.position <= document.length()) {
                        document.insert(op.position, op.content);
                        currentRevision++;
                        op.revision = currentRevision;
                        return true;
                    }
                    break;
                    
                case DELETE:
                    if (op.position < document.length() && 
                        op.position + op.length <= document.length()) {
                        document.erase(op.position, op.length);
                        currentRevision++;
                        op.revision = currentRevision;
                        return true;
                    }
                    break;
                    
                case RETAIN:
                    // No change to document
                    return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error applying operation: " << e.what() << std::endl;
            return false;
        }
        
        return false;
    }
    
    std::string getDocument() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(docMutex));
        return document;
    }
    
    int getRevision() const {
        return currentRevision;
    }
};

// Client session
struct ClientSession {
    std::string clientId;
    struct lws* wsi;
    int lastAckedRevision;
    std::vector<Operation> pendingOps;
    
    ClientSession(const std::string& id, struct lws* w)
        : clientId(id), wsi(w), lastAckedRevision(0) {}
};

// Global state
static OTEngine otEngine;
static std::map<struct lws*, std::shared_ptr<ClientSession>> clients;
static std::mutex clientsMutex;

// Serialize operation to JSON
std::string serializeOperation(const Operation& op, const std::string& clientId) {
    Json::Value root;
    root["type"] = (op.type == INSERT) ? "insert" : "delete";
    root["position"] = op.position;
    root["revision"] = op.revision;
    root["clientId"] = clientId;
    
    if (op.type == INSERT) {
        root["content"] = op.content;
    } else if (op.type == DELETE) {
        root["length"] = op.length;
    }
    
    Json::StreamWriterBuilder writer;
    return Json::writeString(writer, root);
}

// Parse operation from JSON
Operation parseOperation(const std::string& json) {
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errors;
    std::istringstream stream(json);
    
    if (Json::parseFromStream(reader, stream, &root, &errors)) {
        std::string type = root["type"].asString();
        int position = root["position"].asInt();
        
        if (type == "insert") {
            std::string content = root["content"].asString();
            Operation op(INSERT, position, content);
            op.clientId = root["clientId"].asString();
            return op;
        } else if (type == "delete") {
            int length = root["length"].asInt();
            Operation op(DELETE, position, "", length);
            op.clientId = root["clientId"].asString();
            return op;
        }
    }
    
    return Operation(RETAIN, 0);
}

// Broadcast operation to all clients except sender
void broadcastOperation(const Operation& op, struct lws* sender) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    
    std::string message = serializeOperation(op, op.clientId);
    
    for (auto& [wsi, client] : clients) {
        if (wsi != sender) {
            unsigned char buf[LWS_PRE + 4096];
            memcpy(&buf[LWS_PRE], message.c_str(), message.length());
            lws_write(wsi, &buf[LWS_PRE], message.length(), LWS_WRITE_TEXT);
        }
    }
}

// WebSocket callback
static int callback_collaborative_edit(struct lws* wsi,
                                      enum lws_callback_reasons reason,
                                      void* user, void* in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            std::lock_guard<std::mutex> lock(clientsMutex);
            std::string clientId = "client_" + std::to_string(clients.size() + 1);
            clients[wsi] = std::make_shared<ClientSession>(clientId, wsi);
            
            // Send initial document state
            Json::Value init;
            init["type"] = "init";
            init["content"] = otEngine.getDocument();
            init["revision"] = otEngine.getRevision();
            init["clientId"] = clientId;
            
            Json::StreamWriterBuilder writer;
            std::string msg = Json::writeString(writer, init);
            
            unsigned char buf[LWS_PRE + 4096];
            memcpy(&buf[LWS_PRE], msg.c_str(), msg.length());
            lws_write(wsi, &buf[LWS_PRE], msg.length(), LWS_WRITE_TEXT);
            
            std::cout << "Client connected: " << clientId << std::endl;
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            std::string message((char*)in, len);
            
            // Parse and apply operation
            Operation op = parseOperation(message);
            
            if (op.type != RETAIN) {
                // Apply operation to document
                if (otEngine.applyOperation(op)) {
                    // Broadcast to other clients
                    broadcastOperation(op, wsi);
                    
                    // Send acknowledgment
                    Json::Value ack;
                    ack["type"] = "ack";
                    ack["revision"] = op.revision;
                    
                    Json::StreamWriterBuilder writer;
                    std::string ackMsg = Json::writeString(writer, ack);
                    
                    unsigned char buf[LWS_PRE + 1024];
                    memcpy(&buf[LWS_PRE], ackMsg.c_str(), ackMsg.length());
                    lws_write(wsi, &buf[LWS_PRE], ackMsg.length(), LWS_WRITE_TEXT);
                }
            }
            break;
        }
        
        case LWS_CALLBACK_CLOSED: {
            std::lock_guard<std::mutex> lock(clientsMutex);
            auto it = clients.find(wsi);
            if (it != clients.end()) {
                std::cout << "Client disconnected: " << it->second->clientId << std::endl;
                clients.erase(it);
            }
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "collaborative-edit-protocol",
        callback_collaborative_edit,
        0,
        4096,
    },
    { NULL, NULL, 0, 0 }
};

int main() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = 9001;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    struct lws_context* context = lws_create_context(&info);
    if (!context) {
        std::cerr << "Failed to create WebSocket context" << std::endl;
        return 1;
    }
    
    std::cout << "Collaborative editing server started on port 9001" << std::endl;
    
    // Event loop
    while (true) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

## Rust Implementation

Here's a CRDT-based collaborative editor in Rust using tokio and tokio-tungstenite:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{RwLock, mpsc};
use serde::{Serialize, Deserialize};
use uuid::Uuid;

// CRDT-based collaborative text editor using RGA (Replicated Growable Array)
#[derive(Debug, Clone, Serialize, Deserialize)]
struct CharId {
    client_id: String,
    counter: u64,
}

impl PartialEq for CharId {
    fn eq(&self, other: &Self) -> bool {
        self.client_id == other.client_id && self.counter == other.counter
    }
}

impl Eq for CharId {}

impl std::hash::Hash for CharId {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.client_id.hash(state);
        self.counter.hash(state);
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct Character {
    id: CharId,
    value: char,
    visible: bool,
}

// CRDT Document structure
#[derive(Debug, Clone)]
struct CRDTDocument {
    chars: Vec<Character>,
    client_id: String,
    counter: u64,
}

impl CRDTDocument {
    fn new(client_id: String) -> Self {
        Self {
            chars: Vec::new(),
            client_id,
            counter: 0,
        }
    }
    
    // Insert character at position
    fn insert(&mut self, position: usize, value: char) -> Operation {
        self.counter += 1;
        let char_id = CharId {
            client_id: self.client_id.clone(),
            counter: self.counter,
        };
        
        let character = Character {
            id: char_id.clone(),
            value,
            visible: true,
        };
        
        // Find visible position
        let mut visible_pos = 0;
        let mut actual_pos = 0;
        
        for (i, ch) in self.chars.iter().enumerate() {
            if ch.visible {
                if visible_pos == position {
                    actual_pos = i;
                    break;
                }
                visible_pos += 1;
            }
            if i == self.chars.len() - 1 {
                actual_pos = i + 1;
            }
        }
        
        if self.chars.is_empty() {
            actual_pos = 0;
        }
        
        self.chars.insert(actual_pos, character);
        
        Operation::Insert {
            id: char_id,
            value,
            position,
        }
    }
    
    // Delete character at position
    fn delete(&mut self, position: usize) -> Option<Operation> {
        let mut visible_pos = 0;
        
        for ch in self.chars.iter_mut() {
            if ch.visible {
                if visible_pos == position {
                    ch.visible = false;
                    return Some(Operation::Delete {
                        id: ch.id.clone(),
                    });
                }
                visible_pos += 1;
            }
        }
        
        None
    }
    
    // Apply remote operation
    fn apply_operation(&mut self, op: &Operation) {
        match op {
            Operation::Insert { id, value, .. } => {
                // Check if character already exists
                if self.chars.iter().any(|ch| ch.id == *id) {
                    return;
                }
                
                let character = Character {
                    id: id.clone(),
                    value: *value,
                    visible: true,
                };
                
                // Find insertion position based on causal ordering
                let insert_pos = self.find_insert_position(id);
                self.chars.insert(insert_pos, character);
            }
            Operation::Delete { id } => {
                for ch in self.chars.iter_mut() {
                    if ch.id == *id {
                        ch.visible = false;
                        break;
                    }
                }
            }
        }
    }
    
    // Find correct position for character based on causal ordering
    fn find_insert_position(&self, id: &CharId) -> usize {
        for (i, ch) in self.chars.iter().enumerate() {
            if self.compare_ids(&ch.id, id) > 0 {
                return i;
            }
        }
        self.chars.len()
    }
    
    // Compare character IDs for ordering
    fn compare_ids(&self, a: &CharId, b: &CharId) -> std::cmp::Ordering {
        match a.counter.cmp(&b.counter) {
            std::cmp::Ordering::Equal => a.client_id.cmp(&b.client_id),
            other => other,
        }
    }
    
    // Get visible text
    fn get_text(&self) -> String {
        self.chars
            .iter()
            .filter(|ch| ch.visible)
            .map(|ch| ch.value)
            .collect()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum Operation {
    Insert {
        id: CharId,
        value: char,
        position: usize,
    },
    Delete {
        id: CharId,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum ClientMessage {
    Init {
        content: String,
        client_id: String,
    },
    Operation {
        operation: Operation,
    },
    Sync {
        operations: Vec<Operation>,
    },
}

// Shared state
struct SharedState {
    document: CRDTDocument,
    clients: HashMap<String, mpsc::UnboundedSender<String>>,
}

impl SharedState {
    fn new() -> Self {
        Self {
            document: CRDTDocument::new("server".to_string()),
            clients: HashMap::new(),
        }
    }
}

type State = Arc<RwLock<SharedState>>;

async fn handle_connection(
    stream: TcpStream,
    state: State,
) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    
    let client_id = Uuid::new_v4().to_string();
    let (tx, mut rx) = mpsc::unbounded_channel();
    
    // Add client to state
    {
        let mut state = state.write().await;
        state.clients.insert(client_id.clone(), tx);
        
        // Send initial state
        let init_msg = ClientMessage::Init {
            content: state.document.get_text(),
            client_id: client_id.clone(),
        };
        
        let msg = serde_json::to_string(&init_msg)?;
        ws_sender.send(Message::Text(msg)).await?;
    }
    
    println!("Client {} connected", client_id);
    
    // Spawn task to send messages to client
    let client_id_clone = client_id.clone();
    let send_task = tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if ws_sender.send(Message::Text(msg)).await.is_err() {
                break;
            }
        }
    });
    
    // Handle incoming messages
    while let Some(msg) = ws_receiver.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                if let Ok(client_msg) = serde_json::from_str::<ClientMessage>(&text) {
                    match client_msg {
                        ClientMessage::Operation { operation } => {
                            let mut state = state.write().await;
                            
                            // Apply operation to server document
                            state.document.apply_operation(&operation);
                            
                            // Broadcast to all other clients
                            let broadcast_msg = serde_json::to_string(&ClientMessage::Operation {
                                operation: operation.clone(),
                            })?;
                            
                            for (id, sender) in state.clients.iter() {
                                if id != &client_id {
                                    let _ = sender.send(broadcast_msg.clone());
                                }
                            }
                            
                            println!("Document: {}", state.document.get_text());
                        }
                        _ => {}
                    }
                }
            }
            Ok(Message::Close(_)) => break,
            Err(_) => break,
            _ => {}
        }
    }
    
    // Cleanup
    {
        let mut state = state.write().await;
        state.clients.remove(&client_id);
    }
    
    send_task.abort();
    println!("Client {} disconnected", client_id);
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:9001";
    let listener = TcpListener::bind(addr).await?;
    
    println!("Collaborative editing server listening on {}", addr);
    
    let state: State = Arc::new(RwLock::new(SharedState::new()));
    
    while let Ok((stream, _)) = listener.accept().await {
        let state = state.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream, state).await {
                eprintln!("Error handling connection: {}", e);
            }
        });
    }
    
    Ok(())
}

// Example client usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_crdt_concurrent_inserts() {
        let mut doc1 = CRDTDocument::new("client1".to_string());
        let mut doc2 = CRDTDocument::new("client2".to_string());
        
        // Both clients insert at position 0
        let op1 = doc1.insert(0, 'A');
        let op2 = doc2.insert(0, 'B');
        
        // Apply operations to each other
        doc1.apply_operation(&op2);
        doc2.apply_operation(&op1);
        
        // Both documents should converge to same state
        assert_eq!(doc1.get_text(), doc2.get_text());
        println!("Converged text: {}", doc1.get_text());
    }
}
```

## Summary

Collaborative editing via WebSockets enables real-time multi-user document editing through sophisticated conflict resolution strategies. The two main approaches are:

**Operational Transformation (OT)** transforms operations based on concurrent edits to maintain consistency. It requires careful handling of operation ordering and transformation rules but provides intuitive behavior for text editing.

**Conflict-Free Replicated Data Types (CRDTs)** use specialized data structures that automatically converge to the same state across all replicas without centralized coordination. They're simpler to implement correctly but may require more memory.

Both approaches use WebSockets for low-latency bidirectional communication, maintaining document state across distributed clients while handling network partitions, out-of-order delivery, and concurrent modifications. The choice between OT and CRDT depends on factors including document structure complexity, network topology, consistency requirements, and performance constraints.