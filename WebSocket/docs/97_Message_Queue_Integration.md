# Message Queue Integration with WebSockets

## Overview

Message Queue Integration bridges the real-time, bidirectional communication of WebSockets with the robust, asynchronous messaging capabilities of message brokers like RabbitMQ, Kafka, Redis, and NATS. This integration pattern enables scalable, distributed architectures where WebSocket servers can publish and consume messages from centralized message queues, facilitating microservices communication, event-driven architectures, and horizontal scaling of WebSocket servers.

## Why Integrate WebSockets with Message Queues?

### Key Benefits

1. **Horizontal Scaling**: Multiple WebSocket server instances can share message distribution through a common message broker
2. **Decoupling**: Separates WebSocket connection management from business logic processing
3. **Persistence**: Message queues provide durability and guaranteed delivery
4. **Load Balancing**: Distribute processing across multiple consumers
5. **Fan-out**: Broadcast messages to multiple WebSocket clients across different servers
6. **Integration**: Connect WebSocket applications to existing microservices ecosystems

### Common Use Cases

- Real-time notifications across clustered servers
- Chat applications with message persistence
- Live data streaming from multiple sources
- Event broadcasting in distributed systems
- Metrics and monitoring dashboards

## Architecture Patterns

### 1. Pub/Sub Pattern
WebSocket servers subscribe to topics/exchanges and broadcast received messages to connected clients.

### 2. Work Queue Pattern
WebSocket servers consume tasks from queues and send results back to specific clients.

### 3. RPC Pattern
WebSocket clients send requests that are queued, processed by workers, and responses are routed back through the queue.

---

## C/C++ Implementation Examples

### Example 1: RabbitMQ Integration with libamqpcpp

```c
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define RABBITMQ_HOST "localhost"
#define RABBITMQ_PORT 5672
#define QUEUE_NAME "websocket_messages"

// Global WebSocket context
static struct lws_context *ws_context = NULL;
static struct lws *ws_client = NULL;
static pthread_mutex_t ws_mutex = PTHREAD_MUTEX_INITIALIZER;

// RabbitMQ connection structure
typedef struct {
    amqp_connection_state_t conn;
    amqp_socket_t *socket;
} rabbitmq_conn_t;

// Initialize RabbitMQ connection
rabbitmq_conn_t* rabbitmq_init() {
    rabbitmq_conn_t *rmq = malloc(sizeof(rabbitmq_conn_t));
    
    rmq->conn = amqp_new_connection();
    rmq->socket = amqp_tcp_socket_new(rmq->conn);
    
    if (!rmq->socket) {
        fprintf(stderr, "Failed to create TCP socket\n");
        return NULL;
    }
    
    int status = amqp_socket_open(rmq->socket, RABBITMQ_HOST, RABBITMQ_PORT);
    if (status) {
        fprintf(stderr, "Failed to open TCP socket\n");
        return NULL;
    }
    
    amqp_login(rmq->conn, "/", 0, 131072, 0, 
               AMQP_SASL_METHOD_PLAIN, "guest", "guest");
    amqp_channel_open(rmq->conn, 1);
    amqp_get_rpc_reply(rmq->conn);
    
    // Declare queue
    amqp_queue_declare(rmq->conn, 1, 
                      amqp_cstring_bytes(QUEUE_NAME),
                      0, 1, 0, 0, amqp_empty_table);
    
    return rmq;
}

// Publish message to RabbitMQ
void rabbitmq_publish(rabbitmq_conn_t *rmq, const char *message) {
    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("text/plain");
    props.delivery_mode = 2; // persistent
    
    amqp_basic_publish(rmq->conn, 1,
                      amqp_cstring_bytes(""),
                      amqp_cstring_bytes(QUEUE_NAME),
                      0, 0, &props,
                      amqp_cstring_bytes(message));
}

// Consumer thread for RabbitMQ messages
void* rabbitmq_consumer_thread(void *arg) {
    rabbitmq_conn_t *rmq = (rabbitmq_conn_t*)arg;
    
    amqp_basic_consume(rmq->conn, 1,
                      amqp_cstring_bytes(QUEUE_NAME),
                      amqp_empty_bytes, 0, 1, 0,
                      amqp_empty_table);
    
    while (1) {
        amqp_rpc_reply_t res;
        amqp_envelope_t envelope;
        
        amqp_maybe_release_buffers(rmq->conn);
        res = amqp_consume_message(rmq->conn, &envelope, NULL, 0);
        
        if (res.reply_type == AMQP_RESPONSE_NORMAL) {
            // Forward message to WebSocket clients
            char *msg = malloc(envelope.message.body.len + 1);
            memcpy(msg, envelope.message.body.bytes, envelope.message.body.len);
            msg[envelope.message.body.len] = '\0';
            
            printf("Received from RabbitMQ: %s\n", msg);
            
            // Send to WebSocket (thread-safe)
            pthread_mutex_lock(&ws_mutex);
            if (ws_client) {
                unsigned char buf[LWS_PRE + 512];
                int len = snprintf((char*)&buf[LWS_PRE], 512, "%s", msg);
                lws_write(ws_client, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
            }
            pthread_mutex_unlock(&ws_mutex);
            
            free(msg);
            amqp_destroy_envelope(&envelope);
        }
    }
    
    return NULL;
}

// WebSocket callback
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    rabbitmq_conn_t *rmq = (rabbitmq_conn_t*)lws_context_user(lws_get_context(wsi));
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("WebSocket connection established\n");
            pthread_mutex_lock(&ws_mutex);
            ws_client = wsi;
            pthread_mutex_unlock(&ws_mutex);
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Publish received WebSocket message to RabbitMQ
            {
                char msg[512];
                snprintf(msg, sizeof(msg), "%.*s", (int)len, (char*)in);
                printf("WebSocket received: %s\n", msg);
                rabbitmq_publish(rmq, msg);
            }
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("WebSocket connection closed\n");
            pthread_mutex_lock(&ws_mutex);
            ws_client = NULL;
            pthread_mutex_unlock(&ws_mutex);
            break;
            
        default:
            break;
    }
    
    return 0;
}

int main() {
    // Initialize RabbitMQ
    rabbitmq_conn_t *rmq = rabbitmq_init();
    if (!rmq) {
        fprintf(stderr, "Failed to initialize RabbitMQ\n");
        return 1;
    }
    
    // Start RabbitMQ consumer thread
    pthread_t consumer_thread;
    pthread_create(&consumer_thread, NULL, rabbitmq_consumer_thread, rmq);
    
    // Setup WebSocket server
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    static struct lws_protocols protocols[] = {
        { "mq-protocol", ws_callback, 0, 512 },
        { NULL, NULL, 0, 0 }
    };
    
    info.port = 8080;
    info.protocols = protocols;
    info.user = rmq;
    
    ws_context = lws_create_context(&info);
    if (!ws_context) {
        fprintf(stderr, "Failed to create WebSocket context\n");
        return 1;
    }
    
    printf("WebSocket server started on port 8080\n");
    
    // Service loop
    while (1) {
        lws_service(ws_context, 50);
    }
    
    lws_context_destroy(ws_context);
    return 0;
}
```

### Example 2: Redis Pub/Sub Integration

```cpp
#include <hiredis/hiredis.h>
#include <libwebsockets.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <map>

class RedisPubSubBridge {
private:
    redisContext *pub_ctx;
    redisContext *sub_ctx;
    std::mutex clients_mutex;
    std::map<struct lws*, std::string> clients;
    
public:
    RedisPubSubBridge(const char* host, int port) {
        pub_ctx = redisConnect(host, port);
        sub_ctx = redisConnect(host, port);
        
        if (pub_ctx->err || sub_ctx->err) {
            throw std::runtime_error("Redis connection failed");
        }
    }
    
    ~RedisPubSubBridge() {
        redisFree(pub_ctx);
        redisFree(sub_ctx);
    }
    
    void publish(const std::string& channel, const std::string& message) {
        redisReply *reply = (redisReply*)redisCommand(pub_ctx, 
            "PUBLISH %s %s", channel.c_str(), message.c_str());
        if (reply) freeReplyObject(reply);
    }
    
    void subscribe(const std::string& channel) {
        redisReply *reply = (redisReply*)redisCommand(sub_ctx, 
            "SUBSCRIBE %s", channel.c_str());
        if (reply) freeReplyObject(reply);
    }
    
    void subscriber_loop() {
        redisReply *reply;
        while (1) {
            if (redisGetReply(sub_ctx, (void**)&reply) == REDIS_OK) {
                if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
                    std::string msg(reply->element[2]->str, reply->element[2]->len);
                    
                    // Broadcast to all WebSocket clients
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    for (auto& client : clients) {
                        unsigned char buf[LWS_PRE + 1024];
                        int len = snprintf((char*)&buf[LWS_PRE], 1024, "%s", msg.c_str());
                        lws_write(client.first, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
                    }
                }
                freeReplyObject(reply);
            }
        }
    }
    
    void add_client(struct lws* wsi, const std::string& id) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients[wsi] = id;
    }
    
    void remove_client(struct lws* wsi) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(wsi);
    }
};

static RedisPubSubBridge* bridge = nullptr;

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            bridge->add_client(wsi, "client_" + std::to_string((long)wsi));
            std::cout << "Client connected" << std::endl;
            break;
            
        case LWS_CALLBACK_RECEIVE: {
            std::string msg((char*)in, len);
            bridge->publish("websocket_channel", msg);
            break;
        }
            
        case LWS_CALLBACK_CLOSED:
            bridge->remove_client(wsi);
            std::cout << "Client disconnected" << std::endl;
            break;
            
        default:
            break;
    }
    return 0;
}

int main() {
    bridge = new RedisPubSubBridge("127.0.0.1", 6379);
    bridge->subscribe("websocket_channel");
    
    // Start Redis subscriber thread
    std::thread subscriber([&]() {
        bridge->subscriber_loop();
    });
    
    // WebSocket server setup
    struct lws_protocols protocols[] = {
        { "redis-protocol", ws_callback, 0, 1024 },
        { NULL, NULL, 0, 0 }
    };
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    
    struct lws_context *context = lws_create_context(&info);
    
    while (1) {
        lws_service(context, 50);
    }
    
    return 0;
}
```

---

## Rust Implementation Examples

### Example 1: RabbitMQ with Lapin and Tokio-Tungstenite

```rust
use lapin::{
    options::*, types::FieldTable, BasicProperties, Connection,
    ConnectionProperties, Channel,
};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use tokio::net::{TcpListener, TcpStream};
use futures_util::{SinkExt, StreamExt};
use std::sync::Arc;
use tokio::sync::broadcast;

#[derive(Clone)]
struct RabbitMQBridge {
    channel: Arc<Channel>,
    tx: broadcast::Sender<String>,
}

impl RabbitMQBridge {
    async fn new(rabbitmq_url: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let conn = Connection::connect(
            rabbitmq_url,
            ConnectionProperties::default(),
        ).await?;
        
        let channel = conn.create_channel().await?;
        
        // Declare queue
        channel
            .queue_declare(
                "websocket_messages",
                QueueDeclareOptions {
                    durable: true,
                    ..Default::default()
                },
                FieldTable::default(),
            )
            .await?;
        
        let (tx, _) = broadcast::channel(100);
        
        Ok(RabbitMQBridge {
            channel: Arc::new(channel),
            tx,
        })
    }
    
    async fn publish(&self, message: &str) -> Result<(), Box<dyn std::error::Error>> {
        self.channel
            .basic_publish(
                "",
                "websocket_messages",
                BasicPublishOptions::default(),
                message.as_bytes(),
                BasicProperties::default()
                    .with_content_type("text/plain".into())
                    .with_delivery_mode(2),
            )
            .await?;
        
        Ok(())
    }
    
    async fn consume(&self) {
        let consumer = self.channel
            .basic_consume(
                "websocket_messages",
                "websocket_consumer",
                BasicConsumeOptions::default(),
                FieldTable::default(),
            )
            .await
            .expect("Failed to create consumer");
        
        let tx = self.tx.clone();
        
        tokio::spawn(async move {
            consumer.for_each(|delivery| async {
                if let Ok(delivery) = delivery {
                    if let Ok(msg) = String::from_utf8(delivery.data.clone()) {
                        println!("Received from RabbitMQ: {}", msg);
                        let _ = tx.send(msg);
                    }
                    delivery
                        .ack(BasicAckOptions::default())
                        .await
                        .expect("Failed to ack");
                }
            }).await;
        });
    }
}

async fn handle_websocket(
    stream: TcpStream,
    bridge: RabbitMQBridge,
) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    
    let mut rx = bridge.tx.subscribe();
    
    // Task to forward RabbitMQ messages to WebSocket
    let send_task = tokio::spawn(async move {
        while let Ok(msg) = rx.recv().await {
            if ws_sender.send(Message::Text(msg)).await.is_err() {
                break;
            }
        }
    });
    
    // Task to forward WebSocket messages to RabbitMQ
    let bridge_clone = bridge.clone();
    let receive_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_receiver.next().await {
            if let Message::Text(text) = msg {
                println!("WebSocket received: {}", text);
                let _ = bridge_clone.publish(&text).await;
            }
        }
    });
    
    tokio::select! {
        _ = send_task => {},
        _ = receive_task => {},
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let bridge = RabbitMQBridge::new("amqp://guest:guest@localhost:5672").await?;
    bridge.consume().await;
    
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server listening on ws://127.0.0.1:8080");
    
    while let Ok((stream, _)) = listener.accept().await {
        let bridge_clone = bridge.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_websocket(stream, bridge_clone).await {
                eprintln!("WebSocket error: {}", e);
            }
        });
    }
    
    Ok(())
}
```

### Example 2: Kafka Integration with rdkafka

```rust
use rdkafka::config::ClientConfig;
use rdkafka::consumer::{Consumer, StreamConsumer};
use rdkafka::producer::{FutureProducer, FutureRecord};
use rdkafka::Message as KafkaMessage;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use tokio::net::{TcpListener, TcpStream};
use futures_util::{SinkExt, StreamExt};
use std::sync::Arc;
use tokio::sync::broadcast;
use std::time::Duration;

#[derive(Clone)]
struct KafkaBridge {
    producer: Arc<FutureProducer>,
    topic: String,
    tx: broadcast::Sender<String>,
}

impl KafkaBridge {
    fn new(brokers: &str, topic: &str) -> Self {
        let producer: FutureProducer = ClientConfig::new()
            .set("bootstrap.servers", brokers)
            .set("message.timeout.ms", "5000")
            .create()
            .expect("Failed to create Kafka producer");
        
        let (tx, _) = broadcast::channel(100);
        
        KafkaBridge {
            producer: Arc::new(producer),
            topic: topic.to_string(),
            tx,
        }
    }
    
    async fn publish(&self, message: &str) -> Result<(), Box<dyn std::error::Error>> {
        let record = FutureRecord::to(&self.topic)
            .payload(message)
            .key("websocket");
        
        self.producer
            .send(record, Duration::from_secs(0))
            .await
            .map_err(|(e, _)| e)?;
        
        Ok(())
    }
    
    async fn consume(&self, brokers: &str) {
        let consumer: StreamConsumer = ClientConfig::new()
            .set("group.id", "websocket_group")
            .set("bootstrap.servers", brokers)
            .set("enable.auto.commit", "true")
            .set("auto.offset.reset", "latest")
            .create()
            .expect("Failed to create Kafka consumer");
        
        consumer
            .subscribe(&[&self.topic])
            .expect("Failed to subscribe to topic");
        
        let tx = self.tx.clone();
        
        tokio::spawn(async move {
            let mut stream = consumer.stream();
            while let Some(message) = stream.next().await {
                match message {
                    Ok(msg) => {
                        if let Some(payload) = msg.payload() {
                            if let Ok(text) = String::from_utf8(payload.to_vec()) {
                                println!("Received from Kafka: {}", text);
                                let _ = tx.send(text);
                            }
                        }
                    }
                    Err(e) => eprintln!("Kafka error: {}", e),
                }
            }
        });
    }
}

async fn handle_client(
    stream: TcpStream,
    bridge: KafkaBridge,
) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    
    let mut rx = bridge.tx.subscribe();
    
    // Forward Kafka messages to WebSocket
    let send_task = tokio::spawn(async move {
        while let Ok(msg) = rx.recv().await {
            if ws_sender.send(Message::Text(msg)).await.is_err() {
                break;
            }
        }
    });
    
    // Forward WebSocket messages to Kafka
    let bridge_clone = bridge.clone();
    let receive_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_receiver.next().await {
            if let Message::Text(text) = msg {
                let _ = bridge_clone.publish(&text).await;
            }
        }
    });
    
    tokio::select! {
        _ = send_task => {},
        _ = receive_task => {},
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let brokers = "localhost:9092";
    let topic = "websocket_events";
    
    let bridge = KafkaBridge::new(brokers, topic);
    bridge.consume(brokers).await;
    
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server with Kafka listening on ws://127.0.0.1:8080");
    
    while let Ok((stream, _)) = listener.accept().await {
        let bridge_clone = bridge.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_client(stream, bridge_clone).await {
                eprintln!("Error: {}", e);
            }
        });
    }
    
    Ok(())
}
```

### Example 3: Redis Pub/Sub with redis-rs

```rust
use redis::{Client, Commands, PubSubCommands};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use tokio::net::{TcpListener, TcpStream};
use futures_util::{SinkExt, StreamExt};
use std::sync::Arc;
use tokio::sync::broadcast;

#[derive(Clone)]
struct RedisPubSub {
    client: Arc<Client>,
    channel: String,
    tx: broadcast::Sender<String>,
}

impl RedisPubSub {
    fn new(redis_url: &str, channel: &str) -> Result<Self, redis::RedisError> {
        let client = Client::open(redis_url)?;
        let (tx, _) = broadcast::channel(100);
        
        Ok(RedisPubSub {
            client: Arc::new(client),
            channel: channel.to_string(),
            tx,
        })
    }
    
    fn publish(&self, message: &str) -> Result<(), redis::RedisError> {
        let mut con = self.client.get_connection()?;
        con.publish(&self.channel, message)?;
        Ok(())
    }
    
    async fn subscribe(&self) {
        let client = self.client.clone();
        let channel = self.channel.clone();
        let tx = self.tx.clone();
        
        tokio::task::spawn_blocking(move || {
            let mut con = client.get_connection().expect("Failed to connect");
            let mut pubsub = con.as_pubsub();
            pubsub.subscribe(&channel).expect("Failed to subscribe");
            
            loop {
                let msg = pubsub.get_message().expect("Failed to get message");
                let payload: String = msg.get_payload().expect("Failed to get payload");
                println!("Received from Redis: {}", payload);
                let _ = tx.send(payload);
            }
        });
    }
}

async fn handle_connection(
    stream: TcpStream,
    redis: RedisPubSub,
) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    
    let mut rx = redis.tx.subscribe();
    
    // Redis -> WebSocket
    let send_task = tokio::spawn(async move {
        while let Ok(msg) = rx.recv().await {
            if ws_sender.send(Message::Text(msg)).await.is_err() {
                break;
            }
        }
    });
    
    // WebSocket -> Redis
    let redis_clone = redis.clone();
    let receive_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_receiver.next().await {
            if let Message::Text(text) = msg {
                let _ = redis_clone.publish(&text);
            }
        }
    });
    
    tokio::select! {
        _ = send_task => {},
        _ = receive_task => {},
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let redis = RedisPubSub::new("redis://127.0.0.1/", "websocket_channel")?;
    redis.subscribe().await;
    
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server with Redis listening on ws://127.0.0.1:8080");
    
    while let Ok((stream, _)) = listener.accept().await {
        let redis_clone = redis.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream, redis_clone).await {
                eprintln!("Connection error: {}", e);
            }
        });
    }
    
    Ok(())
}
```

---

## Summary

**Message Queue Integration** creates a powerful architectural pattern that combines WebSocket's real-time capabilities with the reliability, scalability, and persistence of message brokers. This integration enables:

### Core Capabilities
- **Horizontal Scaling**: Multiple WebSocket servers share message distribution through centralized brokers
- **Decoupled Architecture**: Separates connection handling from business logic
- **Guaranteed Delivery**: Message persistence and acknowledgment mechanisms
- **Multi-Protocol Support**: RabbitMQ (AMQP), Kafka (distributed log), Redis (pub/sub), NATS, and more

### Implementation Patterns
1. **Pub/Sub**: Broadcast messages to all connected clients across server instances
2. **Work Queues**: Distribute tasks among workers with load balancing
3. **RPC**: Request-response patterns with queue-based routing
4. **Event Sourcing**: Maintain ordered event logs with Kafka

### Language-Specific Highlights

**C/C++**: Utilizes libraries like `librabbitmq-c` (RabbitMQ), `hiredis` (Redis), and `libwebsockets` with threading for concurrent message handling. Requires manual memory management and thread synchronization.

**Rust**: Leverages async/await with libraries like `lapin` (RabbitMQ), `rdkafka` (Kafka), and `redis-rs`, combined with `tokio-tungstenite` for WebSockets. Provides memory safety and excellent concurrency handling through Tokio's async runtime.

This integration pattern is essential for building production-grade, scalable real-time applications that need to handle thousands of concurrent connections while maintaining message reliability and system resilience.