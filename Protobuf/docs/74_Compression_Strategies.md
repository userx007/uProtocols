# Delta Encoding Patterns in Protocol Buffers

## Overview

Delta encoding is an optimization technique where you transmit only the fields that have changed between successive messages, rather than sending complete state snapshots every time. This is particularly valuable in streaming scenarios, real-time updates, and systems where bandwidth or storage efficiency is critical.

## Core Concepts

**Why Delta Encoding?**
- Reduces bandwidth consumption by 60-90% in typical scenarios
- Minimizes serialization/deserialization overhead
- Enables efficient real-time state synchronization
- Particularly effective for large messages with infrequent field updates

**Key Principles:**
- Protobuf's optional fields and field presence detection make delta encoding natural
- Only serialize fields that have changed since the last update
- Receiver merges deltas with existing state
- Requires careful state management on both client and server

## Implementation Strategies

### Strategy 1: Field Presence Detection

Protobuf automatically tracks which fields are set. You can leverage this for delta encoding:

**Protocol Buffer Definition:**

```protobuf
syntax = "proto3";

message PlayerState {
  optional int32 player_id = 1;
  optional float x_position = 2;
  optional float y_position = 3;
  optional float z_position = 4;
  optional int32 health = 5;
  optional int32 armor = 6;
  optional string weapon = 7;
  optional int64 timestamp = 8;
}
```

### Strategy 2: Explicit Delta Messages

Create dedicated delta message types:

```protobuf
message PlayerStateFull {
  int32 player_id = 1;
  float x_position = 2;
  float y_position = 3;
  float z_position = 4;
  int32 health = 5;
  int32 armor = 6;
  string weapon = 7;
}

message PlayerStateDelta {
  int32 player_id = 1;  // Always required for identification
  optional float x_position = 2;
  optional float y_position = 3;
  optional float z_position = 4;
  optional int32 health = 5;
  optional int32 armor = 6;
  optional string weapon = 7;
  int64 delta_timestamp = 8;
}
```

## C++ Implementation

```cpp
#include <iostream>
#include <unordered_map>
#include <memory>
#include "player_state.pb.h"

class DeltaEncoder {
private:
    std::unordered_map<int32_t, PlayerState> last_states_;

public:
    // Encode only changed fields
    PlayerStateDelta encode_delta(const PlayerState& current_state) {
        PlayerStateDelta delta;
        int32_t player_id = current_state.player_id();
        delta.set_player_id(player_id);
        
        auto it = last_states_.find(player_id);
        
        if (it == last_states_.end()) {
            // First time seeing this player - send everything
            copy_all_fields(current_state, delta);
        } else {
            const PlayerState& last_state = it->second;
            
            // Only set fields that changed
            if (current_state.has_x_position() && 
                current_state.x_position() != last_state.x_position()) {
                delta.set_x_position(current_state.x_position());
            }
            
            if (current_state.has_y_position() && 
                current_state.y_position() != last_state.y_position()) {
                delta.set_y_position(current_state.y_position());
            }
            
            if (current_state.has_z_position() && 
                current_state.z_position() != last_state.z_position()) {
                delta.set_z_position(current_state.z_position());
            }
            
            if (current_state.has_health() && 
                current_state.health() != last_state.health()) {
                delta.set_health(current_state.health());
            }
            
            if (current_state.has_armor() && 
                current_state.armor() != last_state.armor()) {
                delta.set_armor(current_state.armor());
            }
            
            if (current_state.has_weapon() && 
                current_state.weapon() != last_state.weapon()) {
                delta.set_weapon(current_state.weapon());
            }
        }
        
        // Update stored state
        last_states_[player_id] = current_state;
        delta.set_delta_timestamp(std::time(nullptr));
        
        return delta;
    }

private:
    void copy_all_fields(const PlayerState& src, PlayerStateDelta& dst) {
        if (src.has_x_position()) dst.set_x_position(src.x_position());
        if (src.has_y_position()) dst.set_y_position(src.y_position());
        if (src.has_z_position()) dst.set_z_position(src.z_position());
        if (src.has_health()) dst.set_health(src.health());
        if (src.has_armor()) dst.set_armor(src.armor());
        if (src.has_weapon()) dst.set_weapon(src.weapon());
    }
};

class DeltaDecoder {
private:
    std::unordered_map<int32_t, PlayerState> current_states_;

public:
    // Apply delta to reconstruct full state
    PlayerState apply_delta(const PlayerStateDelta& delta) {
        int32_t player_id = delta.player_id();
        PlayerState& state = current_states_[player_id];
        
        state.set_player_id(player_id);
        
        // Merge only the fields present in delta
        if (delta.has_x_position()) {
            state.set_x_position(delta.x_position());
        }
        if (delta.has_y_position()) {
            state.set_y_position(delta.y_position());
        }
        if (delta.has_z_position()) {
            state.set_z_position(delta.z_position());
        }
        if (delta.has_health()) {
            state.set_health(delta.health());
        }
        if (delta.has_armor()) {
            state.set_armor(delta.armor());
        }
        if (delta.has_weapon()) {
            state.set_weapon(delta.weapon());
        }
        
        return state;
    }
    
    const PlayerState* get_current_state(int32_t player_id) const {
        auto it = current_states_.find(player_id);
        return (it != current_states_.end()) ? &it->second : nullptr;
    }
};

// Example usage
int main() {
    DeltaEncoder encoder;
    DeltaDecoder decoder;
    
    // Frame 1: Initial state
    PlayerState state1;
    state1.set_player_id(100);
    state1.set_x_position(10.0f);
    state1.set_y_position(20.0f);
    state1.set_z_position(5.0f);
    state1.set_health(100);
    state1.set_armor(50);
    state1.set_weapon("rifle");
    
    PlayerStateDelta delta1 = encoder.encode_delta(state1);
    std::cout << "Delta 1 size: " << delta1.ByteSizeLong() << " bytes\n";
    
    // Frame 2: Only position changed
    PlayerState state2 = state1;
    state2.set_x_position(11.5f);
    state2.set_y_position(21.3f);
    
    PlayerStateDelta delta2 = encoder.encode_delta(state2);
    std::cout << "Delta 2 size: " << delta2.ByteSizeLong() << " bytes "
              << "(only position changed)\n";
    
    // Decode on receiver side
    PlayerState reconstructed1 = decoder.apply_delta(delta1);
    PlayerState reconstructed2 = decoder.apply_delta(delta2);
    
    std::cout << "Reconstructed position: (" 
              << reconstructed2.x_position() << ", "
              << reconstructed2.y_position() << ")\n";
    std::cout << "Reconstructed health: " << reconstructed2.health() << "\n";
    
    return 0;
}
```

## C Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "player_state.pb-c.h"

#define MAX_PLAYERS 1000

typedef struct {
    PlayerState *states[MAX_PLAYERS];
    int count;
} StateCache;

void init_cache(StateCache *cache) {
    memset(cache, 0, sizeof(StateCache));
}

void free_cache(StateCache *cache) {
    for (int i = 0; i < cache->count; i++) {
        if (cache->states[i]) {
            player_state__free_unpacked(cache->states[i], NULL);
        }
    }
}

PlayerState* find_state(StateCache *cache, int32_t player_id) {
    for (int i = 0; i < cache->count; i++) {
        if (cache->states[i] && cache->states[i]->player_id == player_id) {
            return cache->states[i];
        }
    }
    return NULL;
}

PlayerStateDelta* encode_delta(StateCache *cache, const PlayerState *current) {
    PlayerStateDelta *delta = malloc(sizeof(PlayerStateDelta));
    player_state_delta__init(delta);
    
    delta->player_id = current->player_id;
    delta->has_player_id = 1;
    
    PlayerState *last = find_state(cache, current->player_id);
    
    if (!last) {
        // First update - send all fields
        if (current->has_x_position) {
            delta->x_position = current->x_position;
            delta->has_x_position = 1;
        }
        if (current->has_y_position) {
            delta->y_position = current->y_position;
            delta->has_y_position = 1;
        }
        if (current->has_health) {
            delta->health = current->health;
            delta->has_health = 1;
        }
        if (current->has_weapon) {
            delta->weapon = strdup(current->weapon);
        }
        
        // Store state
        if (cache->count < MAX_PLAYERS) {
            size_t size = player_state__get_packed_size(current);
            uint8_t *buf = malloc(size);
            player_state__pack(current, buf);
            cache->states[cache->count++] = player_state__unpack(NULL, size, buf);
            free(buf);
        }
    } else {
        // Send only changes
        if (current->has_x_position && 
            current->x_position != last->x_position) {
            delta->x_position = current->x_position;
            delta->has_x_position = 1;
        }
        if (current->has_health && 
            current->health != last->health) {
            delta->health = current->health;
            delta->has_health = 1;
        }
        
        // Update cached state
        if (current->has_x_position) last->x_position = current->x_position;
        if (current->has_health) last->health = current->health;
    }
    
    delta->delta_timestamp = time(NULL);
    delta->has_delta_timestamp = 1;
    
    return delta;
}

PlayerState* apply_delta(StateCache *cache, const PlayerStateDelta *delta) {
    PlayerState *state = find_state(cache, delta->player_id);
    
    if (!state) {
        state = malloc(sizeof(PlayerState));
        player_state__init(state);
        state->player_id = delta->player_id;
        state->has_player_id = 1;
        
        if (cache->count < MAX_PLAYERS) {
            cache->states[cache->count++] = state;
        }
    }
    
    // Apply changes
    if (delta->has_x_position) {
        state->x_position = delta->x_position;
        state->has_x_position = 1;
    }
    if (delta->has_y_position) {
        state->y_position = delta->y_position;
        state->has_y_position = 1;
    }
    if (delta->has_health) {
        state->health = delta->health;
        state->has_health = 1;
    }
    
    return state;
}

int main() {
    StateCache encoder_cache, decoder_cache;
    init_cache(&encoder_cache);
    init_cache(&decoder_cache);
    
    // Create initial state
    PlayerState state = PLAYER_STATE__INIT;
    state.player_id = 100;
    state.has_player_id = 1;
    state.x_position = 10.0f;
    state.has_x_position = 1;
    state.health = 100;
    state.has_health = 1;
    
    // Encode delta
    PlayerStateDelta *delta1 = encode_delta(&encoder_cache, &state);
    
    size_t packed_size = player_state_delta__get_packed_size(delta1);
    printf("Delta size: %zu bytes\n", packed_size);
    
    // Decode
    PlayerState *reconstructed = apply_delta(&decoder_cache, delta1);
    printf("Reconstructed health: %d\n", reconstructed->health);
    
    player_state_delta__free_unpacked(delta1, NULL);
    free_cache(&encoder_cache);
    free_cache(&decoder_cache);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

// Assuming generated protobuf code
mod proto {
    include!("player_state.rs");
}

use proto::{PlayerState, PlayerStateDelta};

pub struct DeltaEncoder {
    last_states: HashMap<i32, PlayerState>,
}

impl DeltaEncoder {
    pub fn new() -> Self {
        Self {
            last_states: HashMap::new(),
        }
    }
    
    pub fn encode_delta(&mut self, current_state: &PlayerState) -> PlayerStateDelta {
        let player_id = current_state.player_id.unwrap_or(0);
        let mut delta = PlayerStateDelta {
            player_id: Some(player_id),
            delta_timestamp: Some(Self::current_timestamp()),
            ..Default::default()
        };
        
        match self.last_states.get(&player_id) {
            None => {
                // First update - send all fields
                delta.x_position = current_state.x_position;
                delta.y_position = current_state.y_position;
                delta.z_position = current_state.z_position;
                delta.health = current_state.health;
                delta.armor = current_state.armor;
                delta.weapon = current_state.weapon.clone();
            }
            Some(last_state) => {
                // Send only changes
                if current_state.x_position != last_state.x_position {
                    delta.x_position = current_state.x_position;
                }
                if current_state.y_position != last_state.y_position {
                    delta.y_position = current_state.y_position;
                }
                if current_state.z_position != last_state.z_position {
                    delta.z_position = current_state.z_position;
                }
                if current_state.health != last_state.health {
                    delta.health = current_state.health;
                }
                if current_state.armor != last_state.armor {
                    delta.armor = current_state.armor;
                }
                if current_state.weapon != last_state.weapon {
                    delta.weapon = current_state.weapon.clone();
                }
            }
        }
        
        // Update cache
        self.last_states.insert(player_id, current_state.clone());
        
        delta
    }
    
    fn current_timestamp() -> i64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as i64
    }
}

pub struct DeltaDecoder {
    current_states: HashMap<i32, PlayerState>,
}

impl DeltaDecoder {
    pub fn new() -> Self {
        Self {
            current_states: HashMap::new(),
        }
    }
    
    pub fn apply_delta(&mut self, delta: &PlayerStateDelta) -> PlayerState {
        let player_id = delta.player_id.unwrap_or(0);
        
        let state = self.current_states
            .entry(player_id)
            .or_insert_with(|| PlayerState {
                player_id: Some(player_id),
                ..Default::default()
            });
        
        // Merge delta fields
        if delta.x_position.is_some() {
            state.x_position = delta.x_position;
        }
        if delta.y_position.is_some() {
            state.y_position = delta.y_position;
        }
        if delta.z_position.is_some() {
            state.z_position = delta.z_position;
        }
        if delta.health.is_some() {
            state.health = delta.health;
        }
        if delta.armor.is_some() {
            state.armor = delta.armor;
        }
        if delta.weapon.is_some() {
            state.weapon = delta.weapon.clone();
        }
        
        state.clone()
    }
    
    pub fn get_current_state(&self, player_id: i32) -> Option<&PlayerState> {
        self.current_states.get(&player_id)
    }
}

// Example usage
fn main() {
    let mut encoder = DeltaEncoder::new();
    let mut decoder = DeltaDecoder::new();
    
    // Frame 1: Initial state
    let state1 = PlayerState {
        player_id: Some(100),
        x_position: Some(10.0),
        y_position: Some(20.0),
        z_position: Some(5.0),
        health: Some(100),
        armor: Some(50),
        weapon: Some("rifle".to_string()),
        ..Default::default()
    };
    
    let delta1 = encoder.encode_delta(&state1);
    let delta1_bytes = prost::Message::encode_to_vec(&delta1);
    println!("Delta 1 size: {} bytes", delta1_bytes.len());
    
    // Frame 2: Only position changed
    let state2 = PlayerState {
        x_position: Some(11.5),
        y_position: Some(21.3),
        ..state1.clone()
    };
    
    let delta2 = encoder.encode_delta(&state2);
    let delta2_bytes = prost::Message::encode_to_vec(&delta2);
    println!("Delta 2 size: {} bytes (only position changed)", delta2_bytes.len());
    
    // Decode on receiver side
    let reconstructed1 = decoder.apply_delta(&delta1);
    let reconstructed2 = decoder.apply_delta(&delta2);
    
    println!(
        "Reconstructed position: ({}, {})",
        reconstructed2.x_position.unwrap(),
        reconstructed2.y_position.unwrap()
    );
    println!("Reconstructed health: {}", reconstructed2.health.unwrap());
}
```

## Summary

Delta encoding in Protocol Buffers leverages the format's optional field semantics to transmit only changed data between updates. By maintaining state caches on both encoder and decoder sides, you can achieve significant bandwidth savings—typically 60-90% in scenarios with sparse updates. The pattern is especially valuable for real-time applications like multiplayer games, IoT telemetry, collaborative editing, and financial market data feeds. Implementations require careful state management and consideration of edge cases like out-of-order delivery, packet loss (requiring periodic full snapshots), and synchronization on connection. The technique pairs well with Protobuf's efficient binary encoding and can be combined with compression for even greater efficiency.