# Asynchronous MQTT in C

## Overview

Asynchronous MQTT is a non-blocking approach to MQTT communication that allows applications to perform other tasks while waiting for network operations to complete. Unlike synchronous operations that block program execution until completion, asynchronous MQTT uses callbacks and event-driven programming to handle responses, making it ideal for applications requiring high responsiveness or managing multiple concurrent connections.

## Key Concepts

### Non-Blocking Operations

In asynchronous MQTT:
- Function calls return immediately without waiting for completion
- The application continues executing while network operations occur in the background
- Results are delivered through callback functions
- No thread blocking occurs during network I/O

### Callback Mechanism

Callbacks are user-defined functions invoked when specific events occur:
- **Connection callbacks**: Triggered when connection succeeds or fails
- **Message arrival callbacks**: Invoked when messages are received
- **Delivery callbacks**: Called when message publishing completes
- **Disconnection callbacks**: Executed when disconnection occurs

### Event Loop Integration

Asynchronous MQTT often integrates with event loops, allowing:
- Integration with GUI frameworks
- Coordination with other asynchronous I/O operations
- Efficient resource utilization in single-threaded applications

## Programming Implementations

### C Implementation using Paho MQTT

The Eclipse Paho MQTT C library provides robust asynchronous support through `MQTTAsync`.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTAsync.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "AsyncExampleClient"
#define TOPIC       "sensors/temperature"
#define QOS         1
#define TIMEOUT     10000L

volatile int finished = 0;

// Callback when connection is lost
void connlost(void *context, char *cause) {
    printf("\nConnection lost: %s\n", cause);
    finished = 1;
}

// Callback when message arrives
int msgarrvd(void *context, char *topicName, int topicLen, 
             MQTTAsync_message *message) {
    printf("Message arrived on topic %s: %.*s\n", 
           topicName, message->payloadlen, (char*)message->payload);
    
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

// Callback on successful publish
void onSendSuccess(void* context, MQTTAsync_successData* response) {
    printf("Message with token %d delivered successfully\n", 
           response->token);
}

// Callback on publish failure
void onSendFailure(void* context, MQTTAsync_failureData* response) {
    printf("Message send failed, token %d, error code %d\n", 
           response->token, response->code);
    finished = 1;
}

// Callback on successful connection
void onConnect(void* context, MQTTAsync_successData* response) {
    MQTTAsync client = (MQTTAsync)context;
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
    int rc;

    printf("Connected successfully\n");

    // Subscribe to topic
    printf("Subscribing to topic %s\n", TOPIC);
    opts.onSuccess = NULL;
    opts.onFailure = NULL;
    opts.context = client;

    if ((rc = MQTTAsync_subscribe(client, TOPIC, QOS, &opts)) 
        != MQTTASYNC_SUCCESS) {
        printf("Failed to subscribe, return code %d\n", rc);
        finished = 1;
    }

    // Publish a message
    pubmsg.payload = "22.5";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    opts.onSuccess = onSendSuccess;
    opts.onFailure = onSendFailure;
    opts.context = client;

    if ((rc = MQTTAsync_sendMessage(client, TOPIC, &pubmsg, &opts)) 
        != MQTTASYNC_SUCCESS) {
        printf("Failed to send message, return code %d\n", rc);
        finished = 1;
    }
}

// Callback on connection failure
void onConnectFailure(void* context, MQTTAsync_failureData* response) {
    printf("Connect failed, rc %d\n", response ? response->code : 0);
    finished = 1;
}

int main(int argc, char* argv[]) {
    MQTTAsync client;
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    int rc;

    // Create async client
    if ((rc = MQTTAsync_create(&client, ADDRESS, CLIENTID, 
                               MQTTCLIENT_PERSISTENCE_NONE, NULL)) 
        != MQTTASYNC_SUCCESS) {
        printf("Failed to create client, return code %d\n", rc);
        return EXIT_FAILURE;
    }

    // Set callbacks
    if ((rc = MQTTAsync_setCallbacks(client, client, connlost, 
                                     msgarrvd, NULL)) 
        != MQTTASYNC_SUCCESS) {
        printf("Failed to set callbacks, return code %d\n", rc);
        MQTTAsync_destroy(&client);
        return EXIT_FAILURE;
    }

    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.onSuccess = onConnect;
    conn_opts.onFailure = onConnectFailure;
    conn_opts.context = client;

    // Initiate connection (non-blocking)
    if ((rc = MQTTAsync_connect(client, &conn_opts)) 
        != MQTTASYNC_SUCCESS) {
        printf("Failed to start connect, return code %d\n", rc);
        MQTTAsync_destroy(&client);
        return EXIT_FAILURE;
    }

    printf("Waiting for events...\n");

    // Keep application running to process callbacks
    while (!finished) {
        #ifdef _WIN32
            Sleep(100);
        #else
            usleep(100000L);
        #endif
    }

    // Cleanup
    MQTTAsync_disconnect(client, NULL);
    MQTTAsync_destroy(&client);
    return EXIT_SUCCESS;
}
```

### C++ Asynchronous Implementation

```cpp
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS{"tcp://broker.hivemq.com:1883"};
const std::string CLIENT_ID{"AsyncCppClient"};
const std::string TOPIC{"sensors/humidity"};
const int QOS = 1;

// Callback class for handling MQTT events
class callback : public virtual mqtt::callback {
    mqtt::async_client& client_;

public:
    callback(mqtt::async_client& cli) : client_(cli) {}

    void connection_lost(const std::string& cause) override {
        std::cout << "\nConnection lost";
        if (!cause.empty())
            std::cout << ": " << cause;
        std::cout << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived" << std::endl;
        std::cout << "\tTopic: " << msg->get_topic() << std::endl;
        std::cout << "\tPayload: " << msg->to_string() << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {
        std::cout << "Delivery complete for token: " 
                  << (token ? token->get_message_id() : -1) << std::endl;
    }
};

// Action listener for asynchronous operations
class action_listener : public virtual mqtt::iaction_listener {
    std::string name_;

    void on_failure(const mqtt::token& tok) override {
        std::cout << name_ << " failure";
        if (tok.get_message_id() != 0)
            std::cout << " for token: " << tok.get_message_id();
        std::cout << std::endl;
    }

    void on_success(const mqtt::token& tok) override {
        std::cout << name_ << " success";
        if (tok.get_message_id() != 0)
            std::cout << " for token: " << tok.get_message_id();
        auto top = tok.get_topics();
        if (top && !top->empty())
            std::cout << "\n\tToken topic: '" << (*top)[0] << "'";
        std::cout << std::endl;
    }

public:
    action_listener(const std::string& name) : name_(name) {}
};

int main(int argc, char* argv[]) {
    try {
        // Create async client
        mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
        
        callback cb(client);
        client.set_callback(cb);

        // Connection options
        auto connOpts = mqtt::connect_options_builder()
            .clean_session(true)
            .keep_alive_interval(std::chrono::seconds(20))
            .finalize();

        std::cout << "Connecting to broker..." << std::endl;

        // Connect asynchronously
        auto tok = client.connect(connOpts);
        tok->wait(); // Wait for connection to complete

        std::cout << "Connected successfully" << std::endl;

        // Subscribe with action listener
        action_listener subListener("Subscription");
        client.subscribe(TOPIC, QOS, nullptr, subListener)->wait();

        // Publish message asynchronously
        action_listener pubListener("Publish");
        auto msg = mqtt::make_message(TOPIC, "65.2");
        msg->set_qos(QOS);
        client.publish(msg, nullptr, pubListener);

        // Keep running to receive messages
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Disconnect
        std::cout << "Disconnecting..." << std::endl;
        client.disconnect()->wait();
        std::cout << "Done" << std::endl;

    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT Error: " << exc.what() << std::endl;
        return 1;
    }

    return 0;
}
```

### Rust Asynchronous Implementation

Rust's async/await syntax provides elegant asynchronous MQTT handling using the `rumqttc` library.

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new("rust_async_client", 
                                           "broker.hivemq.com", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));

    // Create async client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);

    // Spawn task to handle publishing
    let publish_client = client.clone();
    tokio::spawn(async move {
        sleep(Duration::from_secs(1)).await;
        
        // Subscribe to topic
        publish_client
            .subscribe("sensors/pressure", QoS::AtLeastOnce)
            .await
            .unwrap();
        println!("Subscribed to sensors/pressure");

        // Publish messages periodically
        for i in 0..5 {
            let payload = format!("101.{}", i);
            publish_client
                .publish("sensors/pressure", QoS::AtLeastOnce, 
                        false, payload.as_bytes())
                .await
                .unwrap();
            println!("Published: {}", payload);
            sleep(Duration::from_secs(2)).await;
        }
    });

    // Event loop to handle incoming messages
    loop {
        match eventloop.poll().await {
            Ok(notification) => {
                match notification {
                    Event::Incoming(Packet::Publish(p)) => {
                        let payload = String::from_utf8_lossy(&p.payload);
                        println!("Received: {} on topic: {}", 
                                payload, p.topic);
                    }
                    Event::Incoming(Packet::ConnAck(_)) => {
                        println!("Connected to broker");
                    }
                    Event::Incoming(Packet::SubAck(_)) => {
                        println!("Subscription confirmed");
                    }
                    _ => {}
                }
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
    }

    Ok(())
}
```

### Advanced Rust Example with Multiple Subscribers

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::sync::Arc;

#[tokio::main]
async fn main() {
    let mqttoptions = MqttOptions::new("multi_async", 
                                       "broker.hivemq.com", 1883);
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    let client = Arc::new(client);

    // Spawn multiple subscriber/publisher tasks
    for i in 0..3 {
        let client_clone = Arc::clone(&client);
        tokio::spawn(async move {
            let topic = format!("async/task/{}", i);
            
            client_clone.subscribe(&topic, QoS::AtMostOnce)
                .await
                .expect("Subscribe failed");

            loop {
                sleep(Duration::from_secs(3 + i)).await;
                let msg = format!("Message from task {}", i);
                client_clone.publish(&topic, QoS::AtMostOnce, 
                                    false, msg.as_bytes())
                    .await
                    .expect("Publish failed");
            }
        });
    }

    // Handle all events
    loop {
        if let Ok(Event::Incoming(Packet::Publish(p))) = eventloop.poll().await {
            println!("[{}] {}", p.topic, 
                    String::from_utf8_lossy(&p.payload));
        }
    }
}
```

## Summary

Asynchronous MQTT programming enables non-blocking, event-driven communication patterns essential for responsive applications. In C, the Paho library provides callback-based asynchronous operations where developers register handlers for connection, message arrival, and delivery events. C++ builds upon this with object-oriented callback classes and action listeners for cleaner code organization. Rust offers the most modern approach with async/await syntax and the Tokio runtime, allowing natural asynchronous code flow without explicit callback registration.

Key advantages include improved application responsiveness, efficient resource utilization in single-threaded environments, and the ability to handle multiple concurrent MQTT operations. All three languages support the core asynchronous patterns: non-blocking connection establishment, callback-driven message handling, and event loop integration for coordinating with other application components.