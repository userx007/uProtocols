use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::net::UdpSocket;
use tokio::sync::Mutex;
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use bytes::{BytesMut, Buf, BufMut};

// MQTT-SN Message Types
const MQTTSN_ADVERTISE: u8 = 0x00;
const MQTTSN_SEARCHGW: u8 = 0x01;
const MQTTSN_GWINFO: u8 = 0x02;
const MQTTSN_CONNECT: u8 = 0x04;
const MQTTSN_CONNACK: u8 = 0x05;
const MQTTSN_REGISTER: u8 = 0x0A;
const MQTTSN_REGACK: u8 = 0x0B;
const MQTTSN_PUBLISH: u8 = 0x0C;
const MQTTSN_PUBACK: u8 = 0x0D;
const MQTTSN_SUBSCRIBE: u8 = 0x12;
const MQTTSN_SUBACK: u8 = 0x13;
const MQTTSN_PINGREQ: u8 = 0x16;
const MQTTSN_PINGRESP: u8 = 0x17;
const MQTTSN_DISCONNECT: u8 = 0x18;

// Return Codes
const MQTTSN_RC_ACCEPTED: u8 = 0x00;
const MQTTSN_RC_CONGESTION: u8 = 0x01;
const MQTTSN_RC_INVALID_TOPIC_ID: u8 = 0x02;

#[derive(Debug, Clone)]
struct ClientInfo {
    address: SocketAddr,
    client_id: String,
}

struct MqttSnGateway {
    udp_socket: Arc<UdpSocket>,
    mqtt_client: AsyncClient,
    topic_id_map: Arc<Mutex<HashMap<u16, String>>>,
    topic_name_map: Arc<Mutex<HashMap<String, u16>>>,
    client_map: Arc<Mutex<HashMap<String, ClientInfo>>>,
    next_topic_id: Arc<Mutex<u16>>,
}

impl MqttSnGateway {
    async fn new(
        mqtt_host: &str,
        mqtt_port: u16,
        udp_port: u16,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        // Setup MQTT client
        let mut mqtt_options = MqttOptions::new("mqttsn_gateway", mqtt_host, mqtt_port);
        mqtt_options.set_keep_alive(std::time::Duration::from_secs(60));

        let (mqtt_client, mut eventloop) = AsyncClient::new(mqtt_options, 10);

        // Spawn MQTT event loop
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(event) => {
                        if let Event::Incoming(Packet::Publish(p)) = event {
                            println!("Received MQTT message on topic: {}", p.topic);
                        }
                    }
                    Err(e) => {
                        eprintln!("MQTT connection error: {:?}", e);
                        tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
                    }
                }
            }
        });

        // Setup UDP socket
        let udp_addr = format!("0.0.0.0:{}", udp_port);
        let udp_socket = UdpSocket::bind(&udp_addr).await?;

        println!("MQTT-SN Gateway started on UDP port {}", udp_port);
        println!("Connected to MQTT broker at {}:{}", mqtt_host, mqtt_port);

        Ok(Self {
            udp_socket: Arc::new(udp_socket),
            mqtt_client,
            topic_id_map: Arc::new(Mutex::new(HashMap::new())),
            topic_name_map: Arc::new(Mutex::new(HashMap::new())),
            client_map: Arc::new(Mutex::new(HashMap::new())),
            next_topic_id: Arc::new(Mutex::new(1)),
        })
    }

    async fn run(&self) -> Result<(), Box<dyn std::error::Error>> {
        let mut buf = vec![0u8; 1024];

        loop {
            let (len, addr) = self.udp_socket.recv_from(&mut buf).await?;
            
            if len >= 2 {
                let msg_type = buf[1];
                println!("Received MQTT-SN message type: 0x{:02X} from {}", msg_type, addr);
                
                self.handle_message(&buf[..len], addr).await;
            }
        }
    }

    async fn handle_message(&self, data: &[u8], addr: SocketAddr) {
        if data.len() < 2 {
            return;
        }

        let msg_type = data[1];

        match msg_type {
            MQTTSN_SEARCHGW => self.handle_searchgw(addr).await,
            MQTTSN_CONNECT => self.handle_connect(data, addr).await,
            MQTTSN_REGISTER => self.handle_register(data, addr).await,
            MQTTSN_PUBLISH => self.handle_publish(data, addr).await,
            MQTTSN_SUBSCRIBE => self.handle_subscribe(data, addr).await,
            MQTTSN_PINGREQ => self.handle_pingreq(addr).await,
            MQTTSN_DISCONNECT => self.handle_disconnect(addr).await,
            _ => println!("Unknown MQTT-SN message type: 0x{:02X}", msg_type),
        }
    }

    async fn handle_searchgw(&self, addr: SocketAddr) {
        // Send GWINFO
        let mut response = BytesMut::new();
        response.put_u8(3); // length
        response.put_u8(MQTTSN_GWINFO);
        response.put_u8(1); // gateway ID

        if let Err(e) = self.udp_socket.send_to(&response, addr).await {
            eprintln!("Failed to send GWINFO: {}", e);
        }
    }

    async fn handle_connect(&self, data: &[u8], addr: SocketAddr) {
        if data.len() < 6 {
            return;
        }

        let client_id_len = data[0] as usize - 6;
        let client_id = if data.len() >= 6 + client_id_len {
            String::from_utf8_lossy(&data[6..6 + client_id_len]).to_string()
        } else {
            "unknown".to_string()
        };

        // Store client info
        let client_info = ClientInfo {
            address: addr,
            client_id: client_id.clone(),
        };
        
        self.client_map.lock().await.insert(client_id.clone(), client_info);

        // Send CONNACK
        let mut response = BytesMut::new();
        response.put_u8(3); // length
        response.put_u8(MQTTSN_CONNACK);
        response.put_u8(MQTTSN_RC_ACCEPTED);

        if let Err(e) = self.udp_socket.send_to(&response, addr).await {
            eprintln!("Failed to send CONNACK: {}", e);
        }

        println!("Client connected: {} from {}", client_id, addr);
    }

    async fn handle_register(&self, data: &[u8], addr: SocketAddr) {
        if data.len() < 7 {
            return;
        }

        let mut cursor = std::io::Cursor::new(&data[2..]);
        let topic_id_from_client = cursor.get_u16();
        let msg_id = cursor.get_u16();
        
        let topic_name = String::from_utf8_lossy(&data[6..]).trim_end_matches('\0').to_string();

        // Allocate new topic ID
        let mut next_id = self.next_topic_id.lock().await;
        let topic_id = *next_id;
        *next_id += 1;
        drop(next_id);

        // Register topic
        self.topic_id_map.lock().await.insert(topic_id, topic_name.clone());
        self.topic_name_map.lock().await.insert(topic_name.clone(), topic_id);

        // Send REGACK
        let mut response = BytesMut::new();
        response.put_u8(7); // length
        response.put_u8(MQTTSN_REGACK);
        response.put_u16(topic_id);
        response.put_u16(msg_id);
        response.put_u8(MQTTSN_RC_ACCEPTED);

        if let Err(e) = self.udp_socket.send_to(&response, addr).await {
            eprintln!("Failed to send REGACK: {}", e);
        }

        println!("Topic registered: {} -> ID {}", topic_name, topic_id);
    }

    async fn handle_publish(&self, data: &[u8], addr: SocketAddr) {
        if data.len() < 8 {
            return;
        }

        let flags = data[2];
        let topic_id = u16::from_be_bytes([data[3], data[4]]);
        let msg_id = u16::from_be_bytes([data[5], data[6]]);
        let payload = &data[7..];

        let topic_map = self.topic_id_map.lock().await;
        if let Some(topic_name) = topic_map.get(&topic_id) {
            // Publish to MQTT broker
            if let Err(e) = self.mqtt_client
                .publish(topic_name, QoS::AtMostOnce, false, payload)
                .await
            {
                eprintln!("Failed to publish to MQTT: {}", e);
            } else {
                println!("Published to MQTT topic: {}", topic_name);
            }

            // Send PUBACK if QoS > 0
            if flags & 0x60 != 0 {
                let mut response = BytesMut::new();
                response.put_u8(7);
                response.put_u8(MQTTSN_PUBACK);
                response.put_u16(topic_id);
                response.put_u16(msg_id);
                response.put_u8(MQTTSN_RC_ACCEPTED);

                let _ = self.udp_socket.send_to(&response, addr).await;
            }
        }
    }

    async fn handle_subscribe(&self, data: &[u8], addr: SocketAddr) {
        if data.len() < 6 {
            return;
        }

        let msg_id = u16::from_be_bytes([data[3], data[4]]);
        let topic_name = String::from_utf8_lossy(&data[5..]).trim_end_matches('\0').to_string();

        // Subscribe to MQTT broker
        if let Err(e) = self.mqtt_client.subscribe(&topic_name, QoS::AtMostOnce).await {
            eprintln!("Failed to subscribe to MQTT topic: {}", e);
        } else {
            println!("Subscribed to MQTT topic: {}", topic_name);
        }

        // Allocate topic ID if not exists
        let mut name_map = self.topic_name_map.lock().await;
        let topic_id = if let Some(&id) = name_map.get(&topic_name) {
            id
        } else {
            let mut next_id = self.next_topic_id.lock().await;
            let id = *next_id;
            *next_id += 1;
            drop(next_id);

            self.topic_id_map.lock().await.insert(id, topic_name.clone());
            name_map.insert(topic_name.clone(), id);
            id
        };

        // Send SUBACK
        let mut response = BytesMut::new();
        response.put_u8(8);
        response.put_u8(MQTTSN_SUBACK);
        response.put_u8(0); // flags
        response.put_u16(topic_id);
        response.put_u16(msg_id);
        response.put_u8(MQTTSN_RC_ACCEPTED);

        if let Err(e) = self.udp_socket.send_to(&response, addr).await {
            eprintln!("Failed to send SUBACK: {}", e);
        }
    }

    async fn handle_pingreq(&self, addr: SocketAddr) {
        let mut response = BytesMut::new();
        response.put_u8(2);
        response.put_u8(MQTTSN_PINGRESP);

        let _ = self.udp_socket.send_to(&response, addr).await;
    }

    async fn handle_disconnect(&self, addr: SocketAddr) {
        println!("Client disconnected from {}", addr);
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let gateway = MqttSnGateway::new("localhost", 1883, 1884).await?;
    gateway.run().await?;
    Ok(())
}