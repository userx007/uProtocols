# MQTT over WebSockets

## Detailed Description

MQTT over WebSockets is a transport binding that enables MQTT protocol communication to run over the WebSocket protocol (RFC 6455). This is particularly important for environments where traditional TCP connections are restricted or unavailable, such as web browsers and corporate networks with strict firewall policies.

### Key Concepts

**Why WebSockets for MQTT?**
- **Browser Support**: Native MQTT uses TCP sockets which aren't available in browser JavaScript. WebSockets provide the only bidirectional communication mechanism browsers support for MQTT.
- **Firewall Traversal**: Many corporate firewalls block non-HTTP(S) ports. WebSockets use standard HTTP ports (80/443), allowing MQTT traffic to pass through.
- **TLS/SSL Integration**: WebSockets seamlessly integrate with HTTPS, providing encrypted MQTT communication over WSS (WebSocket Secure).

**Protocol Encapsulation**
The MQTT protocol packets are encapsulated within WebSocket frames. The MQTT broker must support WebSocket connections on a specific port (commonly 8083 for WS, 8084 for WSS), separate from the standard MQTT TCP port (1883).

**URI Scheme**
- `ws://broker:8083/mqtt` - Unencrypted WebSocket
- `wss://broker:8084/mqtt` - Encrypted WebSocket (TLS)

## C/C++ Implementation

### Using Paho MQTT C Library

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "ws://broker.hivemq.com:8000/mqtt"
#define CLIENTID    "WebSocketClient_C"
#define TOPIC       "test/websocket"
#define QOS         1
#define TIMEOUT     10000L

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt) {
    printf("Message delivery confirmed\n");
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, 
             MQTTClient_message *message) {
    printf("Message arrived on topic '%s': %.*s\n", 
           topicName, message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    // Create client with WebSocket URI
    if ((rc = MQTTClient_create(&client, ADDRESS, CLIENTID,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to create client, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    // Connect to broker over WebSocket
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to broker via WebSocket\n");

    // Subscribe to topic
    if ((rc = MQTTClient_subscribe(client, TOPIC, QOS)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Publish message
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    char payload[] = "Hello from WebSocket MQTT!";
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    if ((rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token)) 
        != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    // Wait for delivery
    MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message published successfully\n");

    // Keep running to receive messages
    printf("Waiting for messages (press Ctrl+C to exit)...\n");
    while(1) {
        #ifdef _WIN32
            Sleep(1000);
        #else
            sleep(1);
        #endif
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

### Using libwebsockets with Raw MQTT

```cpp
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>

static int callback_mqtt_ws(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("WebSocket connection established\n");
            // Send MQTT CONNECT packet
            {
                unsigned char connect_packet[] = {
                    0x10, 0x1A,  // Fixed header: CONNECT, remaining length
                    0x00, 0x04, 'M', 'Q', 'T', 'T',  // Protocol name
                    0x04,  // Protocol level (MQTT 3.1.1)
                    0x02,  // Connect flags (clean session)
                    0x00, 0x3C,  // Keep alive (60 seconds)
                    0x00, 0x0C,  // Client ID length
                    'W', 'S', 'C', 'l', 'i', 'e', 'n', 't', '_', 'C', 'P', 'P'
                };
                lws_write(wsi, &connect_packet[LWS_PRE], 
                         sizeof(connect_packet) - LWS_PRE, LWS_WRITE_BINARY);
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            printf("Received MQTT packet over WebSocket (%zu bytes)\n", len);
            // Parse MQTT packet from 'in' buffer
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            // Send queued MQTT messages
            break;

        case LWS_CALLBACK_CLOSED:
            printf("WebSocket connection closed\n");
            break;

        default:
            break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    { "mqtt", callback_mqtt_ws, 0, 4096 },
    { NULL, NULL, 0, 0 }
};

int main() {
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo;
    struct lws_context *context;

    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        printf("Failed to create WebSocket context\n");
        return -1;
    }

    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = context;
    ccinfo.address = "broker.hivemq.com";
    ccinfo.port = 8000;
    ccinfo.path = "/mqtt";
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = "mqtt";

    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) {
        printf("Failed to connect WebSocket\n");
        lws_context_destroy(context);
        return -1;
    }

    while (lws_service(context, 1000) >= 0) {
        // Event loop
    }

    lws_context_destroy(context);
    return 0;
}
```

## Rust Implementation

### Using rumqttc Library

```rust
use rumqttc::{MqttOptions, Client, QoS, Transport, Event, Packet};
use std::time::Duration;
use std::thread;

fn main() {
    // Configure MQTT client with WebSocket transport
    let mut mqttoptions = MqttOptions::new("rust_ws_client", "broker.hivemq.com", 8000);
    
    // Set WebSocket transport
    mqttoptions.set_transport(Transport::Ws);
    
    // Set keep alive and connection parameters
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    mqttoptions.set_clean_session(true);
    
    // Create client and event loop
    let (client, mut eventloop) = Client::new(mqttoptions, 10);
    
    // Spawn publisher thread
    thread::spawn(move || {
        // Subscribe to topic
        client.subscribe("test/websocket", QoS::AtLeastOnce).unwrap();
        println!("Subscribed to topic");
        
        thread::sleep(Duration::from_secs(1));
        
        // Publish messages
        for i in 0..5 {
            let payload = format!("WebSocket message {}", i);
            client.publish("test/websocket", QoS::AtLeastOnce, false, payload.as_bytes())
                .unwrap();
            println!("Published: {}", payload);
            thread::sleep(Duration::from_secs(2));
        }
    });
    
    // Process incoming events
    loop {
        match eventloop.poll() {
            Ok(notification) => {
                match notification {
                    Event::Incoming(Packet::ConnAck(_)) => {
                        println!("Connected to broker via WebSocket");
                    }
                    Event::Incoming(Packet::Publish(p)) => {
                        let payload = String::from_utf8_lossy(&p.payload);
                        println!("Received on '{}': {}", p.topic, payload);
                    }
                    Event::Incoming(Packet::SubAck(_)) => {
                        println!("Subscription confirmed");
                    }
                    _ => {}
                }
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                thread::sleep(Duration::from_secs(1));
            }
        }
    }
}
```

### Using Secure WebSocket (WSS)

```rust
use rumqttc::{MqttOptions, Client, QoS, Transport, TlsConfiguration, Event, Packet};
use std::time::Duration;

fn main() {
    let mut mqttoptions = MqttOptions::new("rust_wss_client", "broker.hivemq.com", 8884);
    
    // Configure WebSocket Secure (WSS)
    mqttoptions.set_transport(Transport::Wss(TlsConfiguration::default()));
    mqttoptions.set_keep_alive(Duration::from_secs(30));
    
    let (client, mut eventloop) = Client::new(mqttoptions, 10);
    
    // Subscribe
    client.subscribe("secure/topic", QoS::ExactlyOnce).unwrap();
    
    // Event loop
    for (i, notification) in eventloop.iter().enumerate() {
        match notification {
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("Secure WebSocket connection established");
            }
            Ok(Event::Incoming(Packet::Publish(p))) => {
                println!("Received: {:?}", String::from_utf8_lossy(&p.payload));
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Connection error: {:?}", e);
                break;
            }
        }
        
        if i > 100 { break; }
    }
}
```

### Advanced Rust Example with Custom Headers

```rust
use rumqttc::{MqttOptions, Client, QoS, Transport};
use std::collections::HashMap;
use std::time::Duration;

fn main() {
    let mut mqttoptions = MqttOptions::new(
        "rust_advanced_ws", 
        "mqtt.mybroker.com", 
        8000
    );
    
    // Create WebSocket with custom headers (e.g., authentication)
    let mut headers = HashMap::new();
    headers.insert("Authorization".to_string(), "Bearer my-token-123".to_string());
    headers.insert("X-Custom-Header".to_string(), "custom-value".to_string());
    
    // Set transport with custom path and headers
    mqttoptions.set_transport(Transport::ws_with_headers(
        "/mqtt/path".to_string(),
        headers
    ));
    
    mqttoptions.set_keep_alive(Duration::from_secs(15));
    mqttoptions.set_credentials("username", "password");
    
    let (client, mut eventloop) = Client::new(mqttoptions, 20);
    
    // Use the client as normal
    client.subscribe("custom/#", QoS::AtMostOnce).unwrap();
    
    loop {
        match eventloop.poll() {
            Ok(event) => println!("Event: {:?}", event),
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
    }
}
```

## Summary

MQTT over WebSockets enables MQTT communication in constrained environments like web browsers and through restrictive firewalls. The MQTT protocol is encapsulated within WebSocket frames, using standard HTTP/HTTPS ports for connectivity. This approach provides browser compatibility, firewall traversal, and seamless TLS integration.

**Key implementations:**
- **C/C++**: Paho MQTT C library supports WebSocket URIs natively, or libwebsockets can be used for lower-level control
- **Rust**: The rumqttc library provides excellent WebSocket support with simple transport configuration

**Primary use cases:**
- Web-based dashboards and IoT control panels
- Corporate environments with restrictive firewall policies
- Real-time browser applications requiring bidirectional communication
- Mobile apps using web views
- Cloud-native applications requiring HTTP-based protocols

**Transport options:**
- `ws://` for unencrypted WebSocket (typically port 8000 or 8083)
- `wss://` for encrypted WebSocket Secure (typically port 8884 or 8084)

The WebSocket transport maintains full MQTT feature compatibility including QoS levels, retained messages, last will and testament, and persistent sessions.