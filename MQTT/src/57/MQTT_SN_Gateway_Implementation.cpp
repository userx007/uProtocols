#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mosquitto.h>

// MQTT-SN Message Types
#define MQTTSN_ADVERTISE     0x00
#define MQTTSN_SEARCHGW      0x01
#define MQTTSN_GWINFO        0x02
#define MQTTSN_CONNECT       0x04
#define MQTTSN_CONNACK       0x05
#define MQTTSN_REGISTER      0x0A
#define MQTTSN_REGACK        0x0B
#define MQTTSN_PUBLISH       0x0C
#define MQTTSN_SUBSCRIBE     0x12
#define MQTTSN_SUBACK        0x13
#define MQTTSN_PINGREQ       0x16
#define MQTTSN_PINGRESP      0x17
#define MQTTSN_DISCONNECT    0x18

// MQTT-SN Return Codes
#define MQTTSN_RC_ACCEPTED   0x00
#define MQTTSN_RC_CONGESTION 0x01
#define MQTTSN_RC_INVALID_TOPIC_ID 0x02
#define MQTTSN_RC_NOT_SUPPORTED 0x03

#pragma pack(push, 1)
struct MqttSnHeader {
    uint8_t length;
    uint8_t msg_type;
};

struct MqttSnConnect {
    MqttSnHeader header;
    uint8_t flags;
    uint8_t protocol_id;
    uint16_t duration;
    char client_id[23];
};

struct MqttSnRegister {
    MqttSnHeader header;
    uint16_t topic_id;
    uint16_t msg_id;
    char topic_name[256];
};

struct MqttSnPublish {
    MqttSnHeader header;
    uint8_t flags;
    uint16_t topic_id;
    uint16_t msg_id;
    char data[256];
};
#pragma pack(pop)

class MqttSnGateway {
private:
    int udp_socket;
    struct sockaddr_in gateway_addr;
    struct mosquitto* mqtt_client;
    
    std::map<uint16_t, std::string> topic_id_map;
    std::map<std::string, uint16_t> topic_name_map;
    std::map<std::string, sockaddr_in> client_addresses;
    uint16_t next_topic_id;
    
public:
    MqttSnGateway(const char* mqtt_host, int mqtt_port, int udp_port) 
        : next_topic_id(1) {
        
        // Initialize Mosquitto MQTT client
        mosquitto_lib_init();
        mqtt_client = mosquitto_new("mqttsn_gateway", true, this);
        
        if (mqtt_client == nullptr) {
            throw std::runtime_error("Failed to create MQTT client");
        }
        
        // Set MQTT callbacks
        mosquitto_message_callback_set(mqtt_client, on_mqtt_message_static);
        
        // Connect to MQTT broker
        if (mosquitto_connect(mqtt_client, mqtt_host, mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
            throw std::runtime_error("Failed to connect to MQTT broker");
        }
        
        // Start MQTT loop in background
        mosquitto_loop_start(mqtt_client);
        
        // Create UDP socket for MQTT-SN
        udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket < 0) {
            throw std::runtime_error("Failed to create UDP socket");
        }
        
        memset(&gateway_addr, 0, sizeof(gateway_addr));
        gateway_addr.sin_family = AF_INET;
        gateway_addr.sin_addr.s_addr = INADDR_ANY;
        gateway_addr.sin_port = htons(udp_port);
        
        if (bind(udp_socket, (struct sockaddr*)&gateway_addr, sizeof(gateway_addr)) < 0) {
            throw std::runtime_error("Failed to bind UDP socket");
        }
        
        std::cout << "MQTT-SN Gateway started on UDP port " << udp_port << std::endl;
        std::cout << "Connected to MQTT broker at " << mqtt_host << ":" << mqtt_port << std::endl;
    }
    
    ~MqttSnGateway() {
        mosquitto_loop_stop(mqtt_client, false);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        close(udp_socket);
    }
    
    void run() {
        uint8_t buffer[1024];
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        while (true) {
            ssize_t recv_len = recvfrom(udp_socket, buffer, sizeof(buffer), 0,
                                       (struct sockaddr*)&client_addr, &addr_len);
            
            if (recv_len > 0) {
                handle_mqttsn_message(buffer, recv_len, client_addr);
            }
        }
    }
    
private:
    void handle_mqttsn_message(uint8_t* buffer, ssize_t len, sockaddr_in& client_addr) {
        if (len < 2) return;
        
        MqttSnHeader* header = (MqttSnHeader*)buffer;
        
        std::cout << "Received MQTT-SN message type: 0x" << std::hex 
                  << (int)header->msg_type << std::dec << std::endl;
        
        switch (header->msg_type) {
            case MQTTSN_CONNECT:
                handle_connect(buffer, len, client_addr);
                break;
            case MQTTSN_REGISTER:
                handle_register(buffer, len, client_addr);
                break;
            case MQTTSN_PUBLISH:
                handle_publish(buffer, len, client_addr);
                break;
            case MQTTSN_SUBSCRIBE:
                handle_subscribe(buffer, len, client_addr);
                break;
            case MQTTSN_PINGREQ:
                handle_pingreq(client_addr);
                break;
            case MQTTSN_DISCONNECT:
                handle_disconnect(client_addr);
                break;
        }
    }
    
    void handle_connect(uint8_t* buffer, ssize_t len, sockaddr_in& client_addr) {
        MqttSnConnect* msg = (MqttSnConnect*)buffer;
        std::string client_id(msg->client_id, strnlen(msg->client_id, sizeof(msg->client_id)));
        
        client_addresses[client_id] = client_addr;
        
        // Send CONNACK
        uint8_t connack[3] = {3, MQTTSN_CONNACK, MQTTSN_RC_ACCEPTED};
        sendto(udp_socket, connack, sizeof(connack), 0,
               (struct sockaddr*)&client_addr, sizeof(client_addr));
        
        std::cout << "Client connected: " << client_id << std::endl;
    }
    
    void handle_register(uint8_t* buffer, ssize_t len, sockaddr_in& client_addr) {
        MqttSnRegister* msg = (MqttSnRegister*)buffer;
        std::string topic_name(msg->topic_name, len - 6);
        
        uint16_t topic_id = next_topic_id++;
        topic_id_map[topic_id] = topic_name;
        topic_name_map[topic_name] = topic_id;
        
        // Send REGACK
        uint8_t regack[7];
        regack[0] = 7;
        regack[1] = MQTTSN_REGACK;
        memcpy(&regack[2], &topic_id, 2);
        memcpy(&regack[4], &msg->msg_id, 2);
        regack[6] = MQTTSN_RC_ACCEPTED;
        
        sendto(udp_socket, regack, sizeof(regack), 0,
               (struct sockaddr*)&client_addr, sizeof(client_addr));
        
        std::cout << "Topic registered: " << topic_name << " -> ID " << topic_id << std::endl;
    }
    
    void handle_publish(uint8_t* buffer, ssize_t len, sockaddr_in& client_addr) {
        MqttSnPublish* msg = (MqttSnPublish*)buffer;
        uint16_t topic_id = ntohs(msg->topic_id);
        
        if (topic_id_map.find(topic_id) != topic_id_map.end()) {
            std::string topic = topic_id_map[topic_id];
            int data_len = len - 7;
            
            // Publish to MQTT broker
            mosquitto_publish(mqtt_client, nullptr, topic.c_str(),
                            data_len, msg->data, 0, false);
            
            std::cout << "Published to MQTT: " << topic << std::endl;
        }
    }
    
    void handle_subscribe(uint8_t* buffer, ssize_t len, sockaddr_in& client_addr) {
        // Extract topic name and subscribe to MQTT broker
        std::string topic((char*)buffer + 5, len - 5);
        
        mosquitto_subscribe(mqtt_client, nullptr, topic.c_str(), 0);
        
        // Send SUBACK
        uint8_t suback[8];
        suback[0] = 8;
        suback[1] = MQTTSN_SUBACK;
        suback[7] = MQTTSN_RC_ACCEPTED;
        
        sendto(udp_socket, suback, sizeof(suback), 0,
               (struct sockaddr*)&client_addr, sizeof(client_addr));
        
        std::cout << "Subscribed to topic: " << topic << std::endl;
    }
    
    void handle_pingreq(sockaddr_in& client_addr) {
        uint8_t pingresp[2] = {2, MQTTSN_PINGRESP};
        sendto(udp_socket, pingresp, sizeof(pingresp), 0,
               (struct sockaddr*)&client_addr, sizeof(client_addr));
    }
    
    void handle_disconnect(sockaddr_in& client_addr) {
        std::cout << "Client disconnected" << std::endl;
    }
    
    static void on_mqtt_message_static(struct mosquitto* mosq, void* obj,
                                      const struct mosquitto_message* msg) {
        ((MqttSnGateway*)obj)->on_mqtt_message(msg);
    }
    
    void on_mqtt_message(const struct mosquitto_message* msg) {
        // Forward MQTT message to MQTT-SN clients
        std::string topic(msg->topic);
        
        if (topic_name_map.find(topic) != topic_name_map.end()) {
            uint16_t topic_id = topic_name_map[topic];
            
            // Create MQTT-SN PUBLISH message
            std::vector<uint8_t> publish_msg(7 + msg->payloadlen);
            publish_msg[0] = publish_msg.size();
            publish_msg[1] = MQTTSN_PUBLISH;
            publish_msg[2] = 0; // flags
            memcpy(&publish_msg[3], &topic_id, 2);
            memcpy(&publish_msg[7], msg->payload, msg->payloadlen);
            
            // Broadcast to all registered clients
            for (auto& client : client_addresses) {
                sendto(udp_socket, publish_msg.data(), publish_msg.size(), 0,
                       (struct sockaddr*)&client.second, sizeof(client.second));
            }
            
            std::cout << "Forwarded MQTT message to MQTT-SN clients: " << topic << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    try {
        MqttSnGateway gateway("localhost", 1883, 1884);
        gateway.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}