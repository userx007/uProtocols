# Real-Time Gaming with WebSockets

## Detailed Description

Real-time gaming over WebSockets involves creating low-latency, bidirectional communication channels between game clients and servers to synchronize game state, handle player inputs, and maintain consistent gameplay experiences across multiple players. WebSockets are ideal for real-time gaming because they provide:

- **Persistent connections** that eliminate HTTP polling overhead
- **Full-duplex communication** allowing simultaneous bidirectional data flow
- **Low latency** critical for responsive gameplay
- **Efficient binary data transmission** for optimized network performance

### Key Components

**Game State Synchronization**: The server maintains the authoritative game state and broadcasts updates to all connected clients. Clients send their inputs to the server, which processes them, updates the game state, and distributes the changes.

**Input Handling**: Player actions (movement, attacks, interactions) are captured on the client, sent to the server for validation and processing, then the results are distributed to relevant clients.

**Latency Compensation Techniques**:
- **Client-side prediction**: Clients immediately apply their own inputs locally before server confirmation
- **Server reconciliation**: Clients correct their state when server updates arrive
- **Interpolation**: Smoothing other players' movements between server updates
- **Dead reckoning**: Extrapolating entity positions when updates are delayed

**Message Optimization**: Using binary protocols (like MessagePack or Protocol Buffers) instead of JSON to reduce bandwidth and improve parsing speed.

## C/C++ Implementation

### Server Example (using libwebsockets)

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PLAYERS 100
#define GAME_TICK_MS 50  // 20 updates per second

// Game state structure
typedef struct {
    int player_id;
    float x, y;
    float velocity_x, velocity_y;
    int health;
    uint64_t last_input_seq;
} Player;

typedef struct {
    Player players[MAX_PLAYERS];
    int player_count;
    uint64_t game_tick;
} GameState;

// Per-session data
struct session_data {
    int player_id;
    struct lws *wsi;
};

static GameState game_state = {0};
static struct lws_context *context;

// Binary message format for player input
typedef struct __attribute__((packed)) {
    uint8_t msg_type;  // 1 = input
    uint64_t input_seq;
    float move_x;
    float move_y;
    uint8_t action;  // bit flags for actions
} InputMessage;

// Binary message format for state update
typedef struct __attribute__((packed)) {
    uint8_t msg_type;  // 2 = state update
    uint64_t game_tick;
    int player_id;
    float x, y;
    float velocity_x, velocity_y;
    int health;
} StateUpdateMessage;

// Process player input (server-authoritative)
void process_input(int player_id, InputMessage *input) {
    if (player_id < 0 || player_id >= game_state.player_count) return;
    
    Player *player = &game_state.players[player_id];
    
    // Validate and apply input
    float speed = 5.0f;
    player->velocity_x = input->move_x * speed;
    player->velocity_y = input->move_y * speed;
    
    // Update position
    player->x += player->velocity_x * (GAME_TICK_MS / 1000.0f);
    player->y += player->velocity_y * (GAME_TICK_MS / 1000.0f);
    
    // Boundary checking
    if (player->x < 0) player->x = 0;
    if (player->x > 1000) player->x = 1000;
    if (player->y < 0) player->y = 0;
    if (player->y > 1000) player->y = 1000;
    
    player->last_input_seq = input->input_seq;
}

// Broadcast game state to all clients
void broadcast_state(struct lws *exclude_wsi) {
    StateUpdateMessage msg;
    msg.msg_type = 2;
    msg.game_tick = game_state.game_tick;
    
    unsigned char buf[LWS_PRE + sizeof(StateUpdateMessage)];
    
    for (int i = 0; i < game_state.player_count; i++) {
        Player *player = &game_state.players[i];
        
        msg.player_id = player->player_id;
        msg.x = player->x;
        msg.y = player->y;
        msg.velocity_x = player->velocity_x;
        msg.velocity_y = player->velocity_y;
        msg.health = player->health;
        
        memcpy(&buf[LWS_PRE], &msg, sizeof(StateUpdateMessage));
        
        // Broadcast to all connected clients
        lws_callback_on_writable_all_protocol(context,
            lws_get_protocol(exclude_wsi));
    }
}

static int callback_game_protocol(struct lws *wsi,
                                  enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Client connected\n");
            // Assign player ID
            session->player_id = game_state.player_count++;
            session->wsi = wsi;
            
            // Initialize player
            Player *new_player = &game_state.players[session->player_id];
            new_player->player_id = session->player_id;
            new_player->x = 500.0f;
            new_player->y = 500.0f;
            new_player->health = 100;
            break;
            
        case LWS_CALLBACK_RECEIVE:
            if (len >= sizeof(InputMessage)) {
                InputMessage *input = (InputMessage *)in;
                if (input->msg_type == 1) {
                    process_input(session->player_id, input);
                }
            }
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // Send state updates (called by game loop)
            broadcast_state(wsi);
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("Client disconnected\n");
            // Remove player from game state
            break;
            
        default:
            break;
    }
    
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "game-protocol",
        callback_game_protocol,
        sizeof(struct session_data),
        1024,  // rx buffer size
    },
    { NULL, NULL, 0, 0 }
};

// Game loop timer callback
static void game_tick_callback(lws_sorted_usec_list_t *sul) {
    game_state.game_tick++;
    
    // Trigger state broadcast to all clients
    lws_callback_on_writable_all_protocol(context,
        &protocols[0]);
    
    // Schedule next tick
    lws_sul_schedule(context, 0, sul, game_tick_callback,
                     GAME_TICK_MS * LWS_US_PER_MS);
}

int main(void) {
    struct lws_context_creation_info info;
    lws_sorted_usec_list_t sul;
    
    memset(&info, 0, sizeof info);
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    
    printf("Game server started on port 8080\n");
    
    // Start game loop
    memset(&sul, 0, sizeof(sul));
    lws_sul_schedule(context, 0, &sul, game_tick_callback,
                     GAME_TICK_MS * LWS_US_PER_MS);
    
    // Event loop
    while (1) {
        lws_service(context, 0);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

### Client Example (C++)

```cpp
#include <libwebsockets.h>
#include <iostream>
#include <cstring>
#include <chrono>

struct GameClient {
    struct lws *wsi;
    int player_id;
    float x, y;
    uint64_t input_seq;
    
    // Client-side prediction
    float predicted_x, predicted_y;
    
    // Input buffer for reconciliation
    struct PendingInput {
        uint64_t seq;
        float move_x, move_y;
    };
    std::vector<PendingInput> pending_inputs;
};

#pragma pack(push, 1)
struct InputMessage {
    uint8_t msg_type;
    uint64_t input_seq;
    float move_x;
    float move_y;
    uint8_t action;
};

struct StateUpdateMessage {
    uint8_t msg_type;
    uint64_t game_tick;
    int player_id;
    float x, y;
    float velocity_x, velocity_y;
    int health;
};
#pragma pack(pop)

static GameClient client;

void apply_input_locally(float move_x, float move_y) {
    // Client-side prediction
    float speed = 5.0f;
    float dt = 0.05f;  // 50ms tick
    
    client.predicted_x += move_x * speed * dt;
    client.predicted_y += move_y * speed * dt;
    
    // Boundary checking
    if (client.predicted_x < 0) client.predicted_x = 0;
    if (client.predicted_x > 1000) client.predicted_x = 1000;
    if (client.predicted_y < 0) client.predicted_y = 0;
    if (client.predicted_y > 1000) client.predicted_y = 1000;
}

void reconcile_with_server(StateUpdateMessage *update) {
    if (update->player_id != client.player_id) {
        // Update other player's position
        std::cout << "Player " << update->player_id 
                  << " at (" << update->x << ", " << update->y << ")\n";
        return;
    }
    
    // Server reconciliation for our player
    client.x = update->x;
    client.y = update->y;
    
    // Replay pending inputs after server state
    client.predicted_x = client.x;
    client.predicted_y = client.y;
    
    for (auto &input : client.pending_inputs) {
        apply_input_locally(input.move_x, input.move_y);
    }
}

static int client_callback(struct lws *wsi,
                          enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            std::cout << "Connected to game server\n";
            client.wsi = wsi;
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (len >= sizeof(StateUpdateMessage)) {
                StateUpdateMessage *update = (StateUpdateMessage *)in;
                if (update->msg_type == 2) {
                    reconcile_with_server(update);
                }
            }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            // Send input to server
            InputMessage input;
            input.msg_type = 1;
            input.input_seq = client.input_seq++;
            input.move_x = 1.0f;  // Example: moving right
            input.move_y = 0.0f;
            input.action = 0;
            
            unsigned char buf[LWS_PRE + sizeof(InputMessage)];
            memcpy(&buf[LWS_PRE], &input, sizeof(InputMessage));
            
            lws_write(wsi, &buf[LWS_PRE], sizeof(InputMessage),
                     LWS_WRITE_BINARY);
            
            // Apply input locally for prediction
            apply_input_locally(input.move_x, input.move_y);
            
            // Store for reconciliation
            client.pending_inputs.push_back({input.input_seq,
                                            input.move_x, input.move_y});
            
            // Keep only recent inputs
            if (client.pending_inputs.size() > 20) {
                client.pending_inputs.erase(client.pending_inputs.begin());
            }
            
            break;
        }
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            std::cerr << "Connection error\n";
            break;
            
        default:
            break;
    }
    
    return 0;
}

int main() {
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo;
    
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = (struct lws_protocols[]){
        {"game-protocol", client_callback, 0, 1024},
        {NULL, NULL, 0, 0}
    };
    
    struct lws_context *context = lws_create_context(&info);
    
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = context;
    ccinfo.address = "localhost";
    ccinfo.port = 8080;
    ccinfo.path = "/";
    ccinfo.protocol = "game-protocol";
    
    lws_client_connect_via_info(&ccinfo);
    
    while (1) {
        lws_service(context, 50);
    }
    
    return 0;
}
```

## Rust Implementation

### Server Example (using tokio-tungstenite)

```rust
use tokio::net::TcpListener;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::sync::{Arc, Mutex};
use std::collections::HashMap;
use tokio::sync::broadcast;
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct InputMessage {
    msg_type: u8,
    input_seq: u64,
    move_x: f32,
    move_y: f32,
    action: u8,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct StateUpdateMessage {
    msg_type: u8,
    game_tick: u64,
    player_id: usize,
    x: f32,
    y: f32,
    velocity_x: f32,
    velocity_y: f32,
    health: i32,
}

#[derive(Clone)]
struct Player {
    player_id: usize,
    x: f32,
    y: f32,
    velocity_x: f32,
    velocity_y: f32,
    health: i32,
    last_input_seq: u64,
}

type GameState = Arc<Mutex<HashMap<usize, Player>>>;

async fn process_input(
    game_state: GameState,
    player_id: usize,
    input: InputMessage,
) {
    let mut state = game_state.lock().unwrap();
    
    if let Some(player) = state.get_mut(&player_id) {
        let speed = 5.0;
        let dt = 0.05; // 50ms
        
        player.velocity_x = input.move_x * speed;
        player.velocity_y = input.move_y * speed;
        
        player.x += player.velocity_x * dt;
        player.y += player.velocity_y * dt;
        
        // Boundary checking
        player.x = player.x.clamp(0.0, 1000.0);
        player.y = player.y.clamp(0.0, 1000.0);
        
        player.last_input_seq = input.input_seq;
    }
}

async fn handle_connection(
    stream: tokio::net::TcpStream,
    game_state: GameState,
    player_id: usize,
    mut broadcast_rx: broadcast::Receiver<StateUpdateMessage>,
) {
    let ws_stream = accept_async(stream).await.expect("Failed to accept");
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    
    // Initialize player
    {
        let mut state = game_state.lock().unwrap();
        state.insert(player_id, Player {
            player_id,
            x: 500.0,
            y: 500.0,
            velocity_x: 0.0,
            velocity_y: 0.0,
            health: 100,
            last_input_seq: 0,
        });
    }
    
    println!("Player {} connected", player_id);
    
    // Spawn task to send state updates
    let gs = game_state.clone();
    tokio::spawn(async move {
        while let Ok(state_update) = broadcast_rx.recv().await {
            let json = serde_json::to_string(&state_update).unwrap();
            if ws_sender.send(Message::Text(json)).await.is_err() {
                break;
            }
        }
    });
    
    // Handle incoming messages
    while let Some(msg) = ws_receiver.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                if let Ok(input) = serde_json::from_str::<InputMessage>(&text) {
                    if input.msg_type == 1 {
                        process_input(gs.clone(), player_id, input).await;
                    }
                }
            }
            Ok(Message::Binary(data)) => {
                // Handle binary messages for better performance
                if data.len() >= 18 {  // Size of InputMessage
                    // Parse binary message manually or use bincode
                    // For brevity, using JSON in this example
                }
            }
            Ok(Message::Close(_)) => break,
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
            _ => {}
        }
    }
    
    // Clean up player
    game_state.lock().unwrap().remove(&player_id);
    println!("Player {} disconnected", player_id);
}

#[tokio::main]
async fn main() {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(&addr).await.expect("Failed to bind");
    println!("Game server listening on: {}", addr);
    
    let game_state: GameState = Arc::new(Mutex::new(HashMap::new()));
    let (broadcast_tx, _) = broadcast::channel::<StateUpdateMessage>(100);
    
    // Game loop
    let gs_clone = game_state.clone();
    let bc_tx = broadcast_tx.clone();
    tokio::spawn(async move {
        let mut game_tick = 0u64;
        let mut interval = tokio::time::interval(
            tokio::time::Duration::from_millis(50)
        );
        
        loop {
            interval.tick().await;
            game_tick += 1;
            
            let state = gs_clone.lock().unwrap();
            for player in state.values() {
                let update = StateUpdateMessage {
                    msg_type: 2,
                    game_tick,
                    player_id: player.player_id,
                    x: player.x,
                    y: player.y,
                    velocity_x: player.velocity_x,
                    velocity_y: player.velocity_y,
                    health: player.health,
                };
                let _ = bc_tx.send(update);
            }
        }
    });
    
    let mut player_id_counter = 0;
    
    while let Ok((stream, _)) = listener.accept().await {
        let game_state = game_state.clone();
        let broadcast_rx = broadcast_tx.subscribe();
        let player_id = player_id_counter;
        player_id_counter += 1;
        
        tokio::spawn(handle_connection(
            stream,
            game_state,
            player_id,
            broadcast_rx,
        ));
    }
}
```

### Client Example (Rust)

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use serde::{Serialize, Deserialize};
use std::collections::VecDeque;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct InputMessage {
    msg_type: u8,
    input_seq: u64,
    move_x: f32,
    move_y: f32,
    action: u8,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct StateUpdateMessage {
    msg_type: u8,
    game_tick: u64,
    player_id: usize,
    x: f32,
    y: f32,
    velocity_x: f32,
    velocity_y: f32,
    health: i32,
}

#[derive(Clone)]
struct PendingInput {
    seq: u64,
    move_x: f32,
    move_y: f32,
}

struct GameClient {
    player_id: Option<usize>,
    x: f32,
    y: f32,
    predicted_x: f32,
    predicted_y: f32,
    input_seq: u64,
    pending_inputs: VecDeque<PendingInput>,
}

impl GameClient {
    fn new() -> Self {
        Self {
            player_id: None,
            x: 500.0,
            y: 500.0,
            predicted_x: 500.0,
            predicted_y: 500.0,
            input_seq: 0,
            pending_inputs: VecDeque::new(),
        }
    }
    
    fn apply_input_locally(&mut self, move_x: f32, move_y: f32) {
        let speed = 5.0;
        let dt = 0.05;
        
        self.predicted_x += move_x * speed * dt;
        self.predicted_y += move_y * speed * dt;
        
        self.predicted_x = self.predicted_x.clamp(0.0, 1000.0);
        self.predicted_y = self.predicted_y.clamp(0.0, 1000.0);
    }
    
    fn reconcile_with_server(&mut self, update: &StateUpdateMessage) {
        if self.player_id == Some(update.player_id) {
            self.x = update.x;
            self.y = update.y;
            
            // Replay pending inputs
            self.predicted_x = self.x;
            self.predicted_y = self.y;
            
            for input in &self.pending_inputs {
                self.apply_input_locally(input.move_x, input.move_y);
            }
        } else {
            println!(
                "Player {} at ({:.2}, {:.2})",
                update.player_id, update.x, update.y
            );
        }
    }
    
    fn send_input(&mut self, move_x: f32, move_y: f32) -> InputMessage {
        let input = InputMessage {
            msg_type: 1,
            input_seq: self.input_seq,
            move_x,
            move_y,
            action: 0,
        };
        
        self.input_seq += 1;
        
        // Apply locally for prediction
        self.apply_input_locally(move_x, move_y);
        
        // Store for reconciliation
        self.pending_inputs.push_back(PendingInput {
            seq: input.input_seq,
            move_x,
            move_y,
        });
        
        // Keep only recent inputs
        if self.pending_inputs.len() > 20 {
            self.pending_inputs.pop_front();
        }
        
        input
    }
}

#[tokio::main]
async fn main() {
    let url = "ws://localhost:8080";
    let (ws_stream, _) = connect_async(url).await.expect("Failed to connect");
    println!("Connected to game server");
    
    let (mut write, mut read) = ws_stream.split();
    let mut client = GameClient::new();
    
    // Spawn input handling task
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(
            tokio::time::Duration::from_millis(50)
        );
        
        loop {
            interval.tick().await;
            
            // Simulate player input (moving right)
            let input = client.send_input(1.0, 0.0);
            let json = serde_json::to_string(&input).unwrap();
            
            if write.send(Message::Text(json)).await.is_err() {
                break;
            }
            
            println!(
                "Position: ({:.2}, {:.2}) | Predicted: ({:.2}, {:.2})",
                client.x, client.y, client.predicted_x, client.predicted_y
            );
        }
    });
    
    // Handle incoming messages
    while let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                if let Ok(update) = serde_json::from_str::<StateUpdateMessage>(&text) {
                    if update.msg_type == 2 {
                        println!("Received state update: {:?}", update);
                    }
                }
            }
            Ok(Message::Close(_)) => break,
            Err(e) => {
                eprintln!("Error: {}", e);
                break;
            }
            _ => {}
        }
    }
}
```

## Summary

Real-time gaming with WebSockets enables responsive, multiplayer gaming experiences through persistent, low-latency connections. The key architecture involves a server maintaining authoritative game state while clients send inputs and receive state updates. Critical techniques include **client-side prediction** (applying inputs locally before server confirmation), **server reconciliation** (correcting client state based on authoritative server updates), and **interpolation** (smoothing movements between updates).

The implementations demonstrate a game loop running at 20Hz (50ms ticks), binary message formats for efficiency, and input buffering for reconciliation. C/C++ provides low-level control ideal for performance-critical game servers, while Rust offers memory safety with comparable performance through its async runtime. Both examples show the essential pattern: clients send inputs, the server processes them authoritatively, and broadcasts state updates to all connected players while clients predict their own movements to maintain responsiveness.