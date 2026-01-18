# Event Sourcing with MQTT: A Comprehensive Guide

## Overview

Event Sourcing is an architectural pattern where state changes are stored as a sequence of events rather than just the current state. When combined with MQTT, it creates powerful event-driven systems that are scalable, auditable, and resilient. This approach treats every state change as an immutable event published to MQTT topics, allowing systems to reconstruct state by replaying events.

## Core Concepts

### Event Sourcing Fundamentals

1. **Events as Source of Truth**: Instead of storing current state, you store all events that led to that state
2. **Event Store**: A persistent log of all events, ordered by time
3. **Event Replay**: Ability to reconstruct any past state by replaying events
4. **Projections**: Read models built from events for querying

### MQTT's Role in Event Sourcing

- **Event Publication**: MQTT topics as event streams
- **Event Distribution**: Publish/Subscribe for event propagation
- **QoS Levels**: Ensuring event delivery guarantees
- **Retained Messages**: Maintaining latest event snapshots
- **Persistent Sessions**: Ensuring no events are missed

## Architecture Patterns

### Topic Structure for Event Sourcing

```
events/{aggregate}/{aggregate_id}/{event_type}
events/order/12345/created
events/order/12345/item_added
events/order/12345/shipped
events/order/12345/delivered

snapshots/{aggregate}/{aggregate_id}
projections/{view_name}/{key}
```

---

## C/C++ Implementation

Using the Paho MQTT C library for event sourcing:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "MQTTClient.h"

#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID "EventSourcingClient"
#define QOS 2  // Exactly once delivery for events

// Event structure
typedef struct {
    char event_id[64];
    char aggregate_id[64];
    char event_type[64];
    char payload[512];
    long timestamp;
    int version;
} Event;

// Aggregate state (example: Order)
typedef struct {
    char order_id[64];
    char status[32];
    double total_amount;
    int item_count;
    int version;
} OrderState;

// Event store implementation
typedef struct {
    Event* events;
    int count;
    int capacity;
} EventStore;

// Initialize event store
EventStore* create_event_store(int initial_capacity) {
    EventStore* store = (EventStore*)malloc(sizeof(EventStore));
    store->events = (Event*)malloc(sizeof(Event) * initial_capacity);
    store->count = 0;
    store->capacity = initial_capacity;
    return store;
}

// Add event to store
void append_event(EventStore* store, Event* event) {
    if (store->count >= store->capacity) {
        store->capacity *= 2;
        store->events = (Event*)realloc(store->events, 
                                       sizeof(Event) * store->capacity);
    }
    memcpy(&store->events[store->count], event, sizeof(Event));
    store->count++;
}

// Create event with metadata
Event create_event(const char* aggregate_id, const char* event_type, 
                   const char* payload, int version) {
    Event event;
    
    // Generate event ID (simple timestamp-based)
    snprintf(event.event_id, sizeof(event.event_id), 
             "%ld_%s", time(NULL), aggregate_id);
    
    strncpy(event.aggregate_id, aggregate_id, sizeof(event.aggregate_id) - 1);
    strncpy(event.event_type, event_type, sizeof(event.event_type) - 1);
    strncpy(event.payload, payload, sizeof(event.payload) - 1);
    event.timestamp = time(NULL);
    event.version = version;
    
    return event;
}

// Serialize event to JSON
void serialize_event(Event* event, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size,
             "{\"event_id\":\"%s\","
             "\"aggregate_id\":\"%s\","
             "\"event_type\":\"%s\","
             "\"payload\":%s,"
             "\"timestamp\":%ld,"
             "\"version\":%d}",
             event->event_id,
             event->aggregate_id,
             event->event_type,
             event->payload,
             event->timestamp,
             event->version);
}

// Apply event to aggregate state
void apply_event(OrderState* state, Event* event) {
    if (strcmp(event->event_type, "OrderCreated") == 0) {
        strncpy(state->order_id, event->aggregate_id, 
                sizeof(state->order_id) - 1);
        strcpy(state->status, "created");
        state->total_amount = 0.0;
        state->item_count = 0;
        state->version = event->version;
    }
    else if (strcmp(event->event_type, "ItemAdded") == 0) {
        double price;
        sscanf(event->payload, "{\"price\":%lf}", &price);
        state->total_amount += price;
        state->item_count++;
        state->version = event->version;
    }
    else if (strcmp(event->event_type, "OrderShipped") == 0) {
        strcpy(state->status, "shipped");
        state->version = event->version;
    }
}

// Rebuild state from events (Event Replay)
OrderState rebuild_state(EventStore* store, const char* aggregate_id) {
    OrderState state = {0};
    
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->events[i].aggregate_id, aggregate_id) == 0) {
            apply_event(&state, &store->events[i]);
        }
    }
    
    return state;
}

// MQTT message callback
int message_arrived(void* context, char* topic, int topic_len, 
                    MQTTClient_message* message) {
    EventStore* store = (EventStore*)context;
    
    printf("Event received on topic: %s\n", topic);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    
    // Parse and store event (simplified parsing)
    Event event;
    // In production, use proper JSON parsing
    sscanf((char*)message->payload, 
           "{\"event_id\":\"%[^\"]\",\"aggregate_id\":\"%[^\"]\","
           "\"event_type\":\"%[^\"]\"",
           event.event_id, event.aggregate_id, event.event_type);
    
    append_event(store, &event);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

// Publish event to MQTT
int publish_event(MQTTClient client, Event* event) {
    char topic[256];
    char payload[1024];
    
    // Build topic: events/{aggregate_id}/{event_type}
    snprintf(topic, sizeof(topic), "events/%s/%s", 
             event->aggregate_id, event->event_type);
    
    serialize_event(event, payload, sizeof(payload));
    
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    
    if (rc == MQTTCLIENT_SUCCESS) {
        MQTTClient_waitForCompletion(client, token, 5000);
        printf("Event published: %s to %s\n", event->event_type, topic);
    }
    
    return rc;
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    EventStore* event_store = create_event_store(100);
    
    // Create MQTT client
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callback
    MQTTClient_setCallbacks(client, event_store, NULL, 
                           message_arrived, NULL);
    
    // Connect
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 0;  // Persistent session
    
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect\n");
        return EXIT_FAILURE;
    }
    
    // Subscribe to all events for an aggregate
    MQTTClient_subscribe(client, "events/order_123/#", QOS);
    
    // Publish some events
    Event event1 = create_event("order_123", "OrderCreated", 
                                "{\"customer\":\"John\"}", 1);
    publish_event(client, &event1);
    append_event(event_store, &event1);
    
    Event event2 = create_event("order_123", "ItemAdded", 
                                "{\"price\":29.99}", 2);
    publish_event(client, &event2);
    append_event(event_store, &event2);
    
    Event event3 = create_event("order_123", "ItemAdded", 
                                "{\"price\":15.50}", 3);
    publish_event(client, &event3);
    append_event(event_store, &event3);
    
    Event event4 = create_event("order_123", "OrderShipped", 
                                "{\"tracking\":\"ABC123\"}", 4);
    publish_event(client, &event4);
    append_event(event_store, &event4);
    
    // Wait for messages
    printf("\nWaiting for events... (Press Ctrl+C to exit)\n");
    sleep(2);
    
    // Rebuild state from events
    printf("\n=== Rebuilding Order State ===\n");
    OrderState current_state = rebuild_state(event_store, "order_123");
    printf("Order ID: %s\n", current_state.order_id);
    printf("Status: %s\n", current_state.status);
    printf("Total Amount: $%.2f\n", current_state.total_amount);
    printf("Item Count: %d\n", current_state.item_count);
    printf("Version: %d\n", current_state.version);
    
    // Cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    free(event_store->events);
    free(event_store);
    
    return EXIT_SUCCESS;
}
```

---

## Rust Implementation

Using the `paho-mqtt` and `serde` crates:

```rust
use paho_mqtt as mqtt;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use uuid::Uuid;

// Event structure with metadata
#[derive(Debug, Clone, Serialize, Deserialize)]
struct Event {
    event_id: String,
    aggregate_id: String,
    aggregate_type: String,
    event_type: String,
    payload: serde_json::Value,
    timestamp: u64,
    version: u32,
    metadata: HashMap<String, String>,
}

impl Event {
    fn new(
        aggregate_id: String,
        aggregate_type: String,
        event_type: String,
        payload: serde_json::Value,
        version: u32,
    ) -> Self {
        Event {
            event_id: Uuid::new_v4().to_string(),
            aggregate_id,
            aggregate_type,
            event_type,
            payload,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs(),
            version,
            metadata: HashMap::new(),
        }
    }

    fn topic(&self) -> String {
        format!(
            "events/{}/{}/{}",
            self.aggregate_type, self.aggregate_id, self.event_type
        )
    }
}

// Order aggregate state
#[derive(Debug, Clone, Default)]
struct OrderState {
    order_id: String,
    customer_id: String,
    status: String,
    items: Vec<OrderItem>,
    total_amount: f64,
    version: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct OrderItem {
    product_id: String,
    quantity: u32,
    price: f64,
}

// Event Store
#[derive(Clone)]
struct EventStore {
    events: Arc<Mutex<Vec<Event>>>,
}

impl EventStore {
    fn new() -> Self {
        EventStore {
            events: Arc::new(Mutex::new(Vec::new())),
        }
    }

    fn append(&self, event: Event) -> Result<(), String> {
        let mut events = self.events.lock().map_err(|e| e.to_string())?;
        events.push(event);
        Ok(())
    }

    fn get_events_for_aggregate(&self, aggregate_id: &str) -> Vec<Event> {
        let events = self.events.lock().unwrap();
        events
            .iter()
            .filter(|e| e.aggregate_id == aggregate_id)
            .cloned()
            .collect()
    }

    fn get_all_events(&self) -> Vec<Event> {
        let events = self.events.lock().unwrap();
        events.clone()
    }
}

// Event Handler trait
trait EventHandler {
    fn apply(&mut self, event: &Event);
}

// Order aggregate with event handling
impl EventHandler for OrderState {
    fn apply(&mut self, event: &Event) {
        match event.event_type.as_str() {
            "OrderCreated" => {
                self.order_id = event.aggregate_id.clone();
                self.customer_id = event.payload["customer_id"]
                    .as_str()
                    .unwrap_or("")
                    .to_string();
                self.status = "created".to_string();
                self.version = event.version;
            }
            "ItemAdded" => {
                let item: OrderItem = serde_json::from_value(event.payload.clone())
                    .unwrap_or_else(|_| OrderItem {
                        product_id: String::new(),
                        quantity: 0,
                        price: 0.0,
                    });
                self.total_amount += item.price * item.quantity as f64;
                self.items.push(item);
                self.version = event.version;
            }
            "ItemRemoved" => {
                if let Some(product_id) = event.payload["product_id"].as_str() {
                    if let Some(pos) = self.items.iter().position(|i| i.product_id == product_id) {
                        let item = self.items.remove(pos);
                        self.total_amount -= item.price * item.quantity as f64;
                    }
                }
                self.version = event.version;
            }
            "OrderShipped" => {
                self.status = "shipped".to_string();
                self.version = event.version;
            }
            "OrderDelivered" => {
                self.status = "delivered".to_string();
                self.version = event.version;
            }
            "OrderCancelled" => {
                self.status = "cancelled".to_string();
                self.version = event.version;
            }
            _ => println!("Unknown event type: {}", event.event_type),
        }
    }
}

// Projection builder
impl OrderState {
    fn from_events(events: &[Event]) -> Self {
        let mut state = OrderState::default();
        for event in events {
            state.apply(event);
        }
        state
    }
}

// MQTT Event Publisher
struct EventPublisher {
    client: mqtt::Client,
}

impl EventPublisher {
    fn new(broker: &str, client_id: &str) -> Result<Self, mqtt::Error> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker)
            .client_id(client_id)
            .finalize();

        let client = mqtt::Client::new(create_opts)?;

        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(std::time::Duration::from_secs(20))
            .clean_session(false) // Persistent session
            .finalize();

        client.connect(conn_opts)?;
        Ok(EventPublisher { client })
    }

    fn publish_event(&self, event: &Event) -> Result<(), mqtt::Error> {
        let topic = event.topic();
        let payload = serde_json::to_string(event).unwrap();

        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(payload)
            .qos(2) // Exactly once delivery
            .finalize();

        self.client.publish(msg)?;
        println!("Published event: {} to {}", event.event_type, event.topic());
        Ok(())
    }
}

// MQTT Event Subscriber
struct EventSubscriber {
    client: mqtt::Client,
    event_store: EventStore,
}

impl EventSubscriber {
    fn new(
        broker: &str,
        client_id: &str,
        event_store: EventStore,
    ) -> Result<Self, mqtt::Error> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker)
            .client_id(client_id)
            .finalize();

        let client = mqtt::Client::new(create_opts)?;

        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(std::time::Duration::from_secs(20))
            .clean_session(false)
            .finalize();

        client.connect(conn_opts)?;
        
        Ok(EventSubscriber {
            client,
            event_store,
        })
    }

    fn subscribe(&self, topics: &[&str]) -> Result<(), mqtt::Error> {
        let qos = vec![2; topics.len()]; // QoS 2 for all topics
        self.client.subscribe_many(topics, &qos)?;
        println!("Subscribed to topics: {:?}", topics);
        Ok(())
    }

    fn start_listening(&self) -> Result<(), mqtt::Error> {
        let rx = self.client.start_consuming();

        for msg_opt in rx.iter() {
            if let Some(msg) = msg_opt {
                self.handle_message(msg);
            }
        }
        Ok(())
    }

    fn handle_message(&self, msg: mqtt::Message) {
        println!("\nReceived event on topic: {}", msg.topic());
        
        if let Ok(event) = serde_json::from_str::<Event>(msg.payload_str().as_ref()) {
            println!("Event: {:?}", event);
            
            if let Err(e) = self.event_store.append(event) {
                eprintln!("Failed to append event: {}", e);
            }
        } else {
            eprintln!("Failed to parse event");
        }
    }
}

// Command handler that generates events
struct OrderCommandHandler {
    publisher: EventPublisher,
    event_store: EventStore,
}

impl OrderCommandHandler {
    fn new(publisher: EventPublisher, event_store: EventStore) -> Self {
        OrderCommandHandler {
            publisher,
            event_store,
        }
    }

    fn create_order(&self, order_id: String, customer_id: String) -> Result<(), String> {
        let current_state = self.load_order_state(&order_id);
        
        if !current_state.order_id.is_empty() {
            return Err("Order already exists".to_string());
        }

        let event = Event::new(
            order_id,
            "order".to_string(),
            "OrderCreated".to_string(),
            serde_json::json!({ "customer_id": customer_id }),
            1,
        );

        self.publisher.publish_event(&event).map_err(|e| e.to_string())?;
        self.event_store.append(event)?;
        Ok(())
    }

    fn add_item(&self, order_id: String, item: OrderItem) -> Result<(), String> {
        let current_state = self.load_order_state(&order_id);
        
        if current_state.order_id.is_empty() {
            return Err("Order not found".to_string());
        }

        let event = Event::new(
            order_id,
            "order".to_string(),
            "ItemAdded".to_string(),
            serde_json::to_value(&item).unwrap(),
            current_state.version + 1,
        );

        self.publisher.publish_event(&event).map_err(|e| e.to_string())?;
        self.event_store.append(event)?;
        Ok(())
    }

    fn ship_order(&self, order_id: String, tracking_number: String) -> Result<(), String> {
        let current_state = self.load_order_state(&order_id);
        
        if current_state.status != "created" {
            return Err(format!("Cannot ship order in status: {}", current_state.status));
        }

        let event = Event::new(
            order_id,
            "order".to_string(),
            "OrderShipped".to_string(),
            serde_json::json!({ "tracking_number": tracking_number }),
            current_state.version + 1,
        );

        self.publisher.publish_event(&event).map_err(|e| e.to_string())?;
        self.event_store.append(event)?;
        Ok(())
    }

    fn load_order_state(&self, order_id: &str) -> OrderState {
        let events = self.event_store.get_events_for_aggregate(order_id);
        OrderState::from_events(&events)
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let broker = "tcp://localhost:1883";
    let event_store = EventStore::new();

    // Create publisher
    let publisher = EventPublisher::new(broker, "event_publisher")?;

    // Create command handler
    let command_handler = OrderCommandHandler::new(publisher, event_store.clone());

    // Execute commands (which generate events)
    println!("=== Creating Order ===");
    command_handler.create_order("order_456".to_string(), "customer_789".to_string())?;

    std::thread::sleep(std::time::Duration::from_millis(100));

    println!("\n=== Adding Items ===");
    command_handler.add_item(
        "order_456".to_string(),
        OrderItem {
            product_id: "prod_001".to_string(),
            quantity: 2,
            price: 29.99,
        },
    )?;

    command_handler.add_item(
        "order_456".to_string(),
        OrderItem {
            product_id: "prod_002".to_string(),
            quantity: 1,
            price: 49.99,
        },
    )?;

    std::thread::sleep(std::time::Duration::from_millis(100));

    println!("\n=== Shipping Order ===");
    command_handler.ship_order("order_456".to_string(), "TRACK123".to_string())?;

    std::thread::sleep(std::time::Duration::from_millis(100));

    // Rebuild state from events
    println!("\n=== Rebuilding Order State from Events ===");
    let current_state = command_handler.load_order_state("order_456");
    println!("Order ID: {}", current_state.order_id);
    println!("Customer: {}", current_state.customer_id);
    println!("Status: {}", current_state.status);
    println!("Items: {}", current_state.items.len());
    println!("Total Amount: ${:.2}", current_state.total_amount);
    println!("Version: {}", current_state.version);

    println!("\n=== Event History ===");
    let all_events = event_store.get_all_events();
    for event in all_events {
        println!(
            "[v{}] {} - {} at {}",
            event.version, event.event_type, event.aggregate_id, event.timestamp
        );
    }

    Ok(())
}
```

---

## Summary

**Event Sourcing with MQTT** combines the power of event-driven architecture with message-based communication to create systems where:

### Key Benefits
- **Complete Audit Trail**: Every state change is recorded as an immutable event
- **Temporal Queries**: Ability to query system state at any point in time
- **Event Replay**: Rebuild current state or create new projections from event history
- **Debugging**: Easy to trace how the system arrived at its current state
- **Scalability**: Distributed event processing across multiple consumers

### Implementation Patterns
1. **Event Structure**: Events contain aggregate ID, type, payload, version, and timestamp
2. **Topic Design**: Hierarchical MQTT topics organize events by aggregate and type
3. **QoS 2**: Use exactly-once delivery to ensure no events are lost or duplicated
4. **Persistent Sessions**: Maintain subscription state across disconnections
5. **Projections**: Build read models by consuming and processing event streams

### Best Practices
- Always include version numbers to handle concurrent modifications
- Use unique event IDs for idempotency
- Implement snapshots for performance optimization
- Separate command handling from event publication
- Design events to be immutable and self-contained
- Consider eventual consistency in distributed scenarios

Event Sourcing with MQTT is ideal for systems requiring strong auditability, complex business workflows, temporal analysis, and distributed event processing.