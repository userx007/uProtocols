// MQTT Rust Example using rumqttc library
// Add to Cargo.toml:
// [dependencies]
// rumqttc = "0.23"
// tokio = { version = "1", features = ["full"] }

use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet, LastWill};
use std::time::Duration;
use tokio::time;

// Publisher example with various MQTT features
async fn mqtt_publisher() -> Result<(), Box<dyn std::error::Error>> {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new("rust_publisher", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    mqttoptions.set_clean_session(true);
    
    // Set Last Will and Testament
    let lwt = LastWill::new(
        "home/sensor/status",
        "offline",
        QoS::AtLeastOnce,
        true, // retained
    );
    mqttoptions.set_last_will(lwt);

    // Create async client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn task to handle events
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("Publisher connected to broker");
                }
                Ok(Event::Incoming(Packet::PubAck(ack))) => {
                    println!("Message acknowledged: pkid={}", ack.pkid);
                }
                Ok(Event::Outgoing(_)) => {},
                Ok(_) => {},
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    time::sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });

    // Wait for connection
    time::sleep(Duration::from_millis(500)).await;

    // Publish messages with different QoS levels
    println!("Publishing messages...\n");
    
    for i in 0..5 {
        let topic = "home/sensor/temperature";
        let payload = format!("Temperature: {:.1}°C", 20.0 + i as f32 * 0.5);
        
        // QoS 0 - Fire and forget
        client.publish(topic, QoS::AtMostOnce, false, payload.clone()).await?;
        println!("Published (QoS 0): {}", payload);
        
        time::sleep(Duration::from_millis(500)).await;
    }

    // Publish with QoS 1 - At least once delivery
    let payload = "Critical: Temperature spike detected!";
    client.publish(
        "home/sensor/alerts",
        QoS::AtLeastOnce,
        false,
        payload
    ).await?;
    println!("\nPublished alert (QoS 1): {}", payload);

    // Publish retained message - stays on broker
    let status = "online";
    client.publish(
        "home/sensor/status",
        QoS::AtLeastOnce,
        true, // retained
        status
    ).await?;
    println!("Published retained status: {}\n", status);

    // Wait for acknowledgments
    time::sleep(Duration::from_secs(2)).await;

    // Disconnect gracefully
    client.disconnect().await?;
    println!("Publisher disconnected");
    
    Ok(())
}

// Subscriber example with pattern matching
async fn mqtt_subscriber() -> Result<(), Box<dyn std::error::Error>> {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new("rust_subscriber", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    // Use persistent session to queue messages when offline
    mqttoptions.set_clean_session(false);

    // Create client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);

    // Subscribe to multiple topics with different QoS levels
    client.subscribe("home/sensor/#", QoS::AtLeastOnce).await?;
    client.subscribe("home/+/temperature", QoS::AtMostOnce).await?;
    client.subscribe("home/sensor/alerts", QoS::ExactlyOnce).await?;
    
    println!("Subscriber connected and subscribed to topics");
    println!("Subscriptions:");
    println!("  - home/sensor/# (QoS 1)");
    println!("  - home/+/temperature (QoS 0)");
    println!("  - home/sensor/alerts (QoS 2)");
    println!("\nWaiting for messages...\n");

    // Event loop to receive messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let topic = publish.topic.clone();
                let payload = String::from_utf8_lossy(&publish.payload);
                let qos = publish.qos;
                let retained = publish.retain;
                
                println!("─────────────────────────────────");
                println!("Topic:    {}", topic);
                println!("Payload:  {}", payload);
                println!("QoS:      {:?}", qos);
                println!("Retained: {}", retained);
                println!("─────────────────────────────────\n");
            }
            Ok(Event::Incoming(Packet::ConnAck(connack))) => {
                println!("Connected! Session present: {}\n", connack.session_present);
            }
            Ok(Event::Incoming(Packet::SubAck(suback))) => {
                println!("Subscription confirmed: pkid={:?}\n", suback.pkid);
            }
            Ok(Event::Outgoing(_)) => {},
            Ok(_) => {},
            Err(e) => {
                eprintln!("Error: {:?}", e);
                time::sleep(Duration::from_secs(1)).await;
            }
        }
    }
}

// Advanced example: Request-Response pattern using MQTT
async fn mqtt_request_response() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("rpc_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Subscribe to response topic
    let response_topic = "home/rpc/response";
    client.subscribe(response_topic, QoS::AtLeastOnce).await?;

    // Spawn event handler
    tokio::spawn(async move {
        loop {
            if let Ok(Event::Incoming(Packet::Publish(p))) = eventloop.poll().await {
                let payload = String::from_utf8_lossy(&p.payload);
                println!("RPC Response: {}", payload);
            }
        }
    });

    time::sleep(Duration::from_millis(500)).await;

    // Send RPC request
    let request = r#"{"method": "getData", "params": {"sensor": "temp01"}}"#;
    client.publish(
        "home/rpc/request",
        QoS::AtLeastOnce,
        false,
        request
    ).await?;
    
    println!("RPC Request sent: {}", request);
    time::sleep(Duration::from_secs(3)).await;

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        println!("Usage: {} [pub|sub|rpc]", args[0]);
        return Ok(());
    }

    match args[1].as_str() {
        "pub" => mqtt_publisher().await?,
        "sub" => mqtt_subscriber().await?,
        "rpc" => mqtt_request_response().await?,
        _ => println!("Invalid argument. Use 'pub', 'sub', or 'rpc'"),
    }

    Ok(())
}