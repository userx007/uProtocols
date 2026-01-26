# MQTT Programming Guide


## Fundamentals

[01. **MQTT Protocol Overview**](docs/01_MQTT_Protocol_Overview.md)<br>
Understanding the publish-subscribe messaging protocol, its architecture, and core principles

[02. **QoS Levels**](docs/02_QoS_Levels.md)<br>
Quality of Service levels (0, 1, 2) and their guarantees for message delivery

[03. **Topics and Topic Wildcards**](docs/03_Topics_and_Topic_Wildcards.md)<br>
Topic naming conventions, hierarchies, and wildcard subscriptions (+ and #)

[04. **Retained Messages**](docs/04_Retained_Messages.md)<br>
How retained messages work and their use cases for state persistence

[05. **Last Will and Testament**](docs/05_Last_Will_and_Testament.md)<br>
Implementing LWT for detecting disconnected clients gracefully


## Connection Management

[06. **Connect and Disconnect Packets**](docs/06_Connect_and_Disconnect_Packets.md)<br>
MQTT connection establishment, authentication, and clean disconnection

[07. **Keep-Alive Mechanism**](docs/07_Keep_Alive_Mechanism.md)<br>
Heartbeat mechanism to maintain connection and detect failures

[08. **Clean Session vs Persistent Session**](docs/08_Clean_Session_vs_Persistent_Session.md)<br>
Session management strategies and their impact on message delivery

[09. **Connection Retry and Backoff**](docs/09_Connection_Retry_and_Backoff.md)<br>
Implementing robust reconnection logic with exponential backoff

[10. **Client Identifiers**](docs/10_Client_Identifiers.md)<br>
Proper client ID generation and management for uniqueness


## Security

[11. **TLS/SSL Encryption**](docs/11_TLS_SSL_Encryption.md)<br>
Implementing encrypted MQTT connections with certificates

[12. **Username and Password Authentication**](docs/12_Username_and_Password_Authentication.md)<br>
Basic authentication mechanisms and credential management

[13. **Certificate-Based Authentication**](docs/13_Certificate_Based_Authentication.md)<br>
Using X.509 certificates for mutual TLS authentication

[14. **Access Control Lists**](docs/14_Access_Control_Lists.md)<br>
Topic-level authorization and permission management

[15. **OAuth2 and JWT Integration**](docs/15_OAuth2_and_JWT_Integration.md)<br>
Modern authentication patterns for MQTT applications


## Advanced Protocol Features

[16. **MQTT v5 Properties**](docs/16_MQTT_v5_Properties.md)<br>
New features in MQTT 5.0 including user properties and reason codes

[17. **Shared Subscriptions**](docs/17_Shared_Subscriptions.md)<br>
Load balancing subscribers with shared subscription groups

[18. **Request-Response Pattern**](docs/18_Request_Response_Pattern.md)<br>
Implementing RPC-style communication over MQTT

[19. **Flow Control**](docs/19_Flow_Control.md)<br>
Managing message flow with receive maximum and topic aliases

[20. **Session Expiry**](docs/20_Session_Expiry.md)<br>
Configuring and handling session expiration in MQTT v5


## C/C++ Implementation

[21. **Paho MQTT C Client**](docs/21_Paho_MQTT_C_Client.md)<br>
Using Eclipse Paho C library for MQTT communication

[22. **Mosquitto C Library**](docs/22_Mosquitto_C_Library.md)<br>
Working with libmosquitto for embedded systems

[23. **Asynchronous MQTT in C**](docs/23_Asynchronous_MQTT_in_C.md)<br>
Non-blocking MQTT operations and callback handling

[24. **Memory Management in C MQTT**](docs/24_Memory_Management_in_C_MQTT.md)<br>
Proper allocation, deallocation, and leak prevention

[25. **Thread Safety in C++ MQTT**](docs/25_Thread_Safety_in_CPP_MQTT.md)<br>
Multi-threaded MQTT applications and synchronization


## Rust Implementation

[26. **Rumqtt Client Library**](docs/26_Rumqtt_Client_Library.md)<br>
Using rumqttc for async MQTT in Rust applications

[27. **Paho MQTT Rust**](docs/27_Paho_MQTT_Rust.md)<br>
Rust bindings for Eclipse Paho MQTT library

[28. **Tokio Integration**](docs/28_Tokio_Integration.md)<br>
Integrating MQTT with Tokio async runtime

[29. **Error Handling in Rust MQTT**](docs/29_Error_Handling_in_Rust_MQTT.md)<br>
Using Result types and proper error propagation

[30. **Zero-Copy Message Processing**](docs/30_Zero_Copy_Message_Processing.md)<br>
Efficient message handling with Rust's ownership system


## Performance and Scalability

[31. **Message Batching**](docs/31_Message_Batching.md)<br>
Optimizing throughput by batching publish operations

[32. **Payload Compression**](docs/32_Payload_Compression.md)<br>
Reducing bandwidth with compressed payloads

[33. **Connection Pooling**](docs/33_Connection_Pooling.md)<br>
Managing multiple connections efficiently

[34. **Broker Clustering**](docs/34_Broker_Clustering.md)<br>
High availability and horizontal scaling strategies

[35. **Message Queue Optimization**](docs/35_Message_Queue_Optimization.md)<br>
Tuning queue sizes and memory usage


## Testing and Debugging

[36. **MQTT Testing Tools**](docs/36_MQTT_Testing_Tools.md)<br>
Using mosquitto_pub, mosquitto_sub, and MQTT Explorer

[37. **Packet Inspection**](docs/37_Packet_Inspection.md)<br>
Analyzing MQTT traffic with Wireshark and tcpdump

[38. **Mock Brokers for Testing**](docs/38_Mock_Brokers_for_Testing.md)<br>
Creating test environments and integration tests

[39. **Load Testing MQTT Systems**](docs/39_Load_Testing_MQTT_Systems.md)<br>
Stress testing with simulated clients and benchmarks

[40. **Logging and Monitoring**](docs/40_Logging_and_Monitoring.md)<br>
Implementing comprehensive logging and metrics collection


## Design Patterns

[41. **Publisher-Subscriber Pattern**](docs/41_Publisher_Subscriber_Pattern.md)<br>
Decoupling components with pub-sub architecture

[42. **Command and Control**](docs/42_Command_and_Control.md)<br>
Implementing device control over MQTT

[43. **Telemetry and Metrics**](docs/43_Telemetry_and_Metrics.md)<br>
Streaming sensor data and system metrics

[44. **Event Sourcing**](docs/44_Event_Sourcing.md)<br>
Building event-driven systems with MQTT

[45. **Gateway Pattern**](docs/45_Gateway_Pattern.md)<br>
Bridging protocols and aggregating edge devices


## Production Considerations

[46. **Message Persistence**](docs/46_Message_Persistence.md)<br>
Ensuring message durability during broker restarts

[47. **Rate Limiting**](docs/47_Rate_Limiting.md)<br>
Protecting systems from message storms

[48. **Circuit Breaker Pattern**](docs/48_Circuit_Breaker_Pattern.md)<br>
Implementing fault tolerance and graceful degradation

[49. **Monitoring and Alerting**](docs/49_Monitoring_and_Alerting.md)<br>
Production monitoring, metrics, and alert strategies

[50. **Disaster Recovery**](docs/50_Disaster_Recovery.md)<br>
Backup strategies, failover, and business continuity planning

## Broker Implementation

[51. **Mosquitto Broker Configuration**](docs/51_Mosquitto_Broker_Configuration.md)<br>
Advanced configuration options, listeners, and optimization for Mosquitto broker

[52. **EMQX Broker Setup**](docs/52_EMQX_Broker_Setup.md)<br>
Enterprise-grade EMQX broker deployment, clustering, and management

[53. **HiveMQ Configuration**](docs/53_HiveMQ_Configuration.md)<br>
Professional MQTT broker setup with extensions and enterprise features

[54. **VerneMQ Clustering**](docs/54_VerneMQ_Clustering.md)<br>
Distributed MQTT broker using VerneMQ with high availability

[55. **Custom Broker Development**](docs/55_Custom_Broker_Development.md)<br>
Building lightweight MQTT brokers for specialized use cases

## MQTT-SN (MQTT for Sensor Networks)

[56. **MQTT-SN Protocol Overview**](docs/56_MQTT_SN_Protocol_Overview.md)<br>
Understanding MQTT for Sensor Networks and UDP-based communication

[57. **MQTT-SN Gateway**](docs/57_MQTT_SN_Gateway.md)<br>
Bridging MQTT-SN devices to standard MQTT brokers

[58. **Sleeping Client Support**](docs/58_Sleeping_Client_Support.md)<br>
Managing battery-powered devices with sleep/wake cycles

[59. **Topic Registration in MQTT-SN**](docs/59_Topic_Registration_in_MQTT_SN.md)<br>
Short topic IDs and registration procedures for constrained devices

[60. **QoS -1 (Quality Minus One)**](docs/60_QoS_Minus_One.md)<br>
Fire-and-forget messaging for extreme low-power scenarios

## Edge Computing and IoT

[61. **MQTT in Edge Devices**](docs/61_MQTT_in_Edge_Devices.md)<br>
Implementing MQTT on resource-constrained embedded systems

[62. **ESP32/ESP8266 MQTT**](docs/62_ESP32_ESP8266_MQTT.md)<br>
MQTT integration with ESP microcontrollers for IoT projects

[63. **Arduino MQTT Libraries**](docs/63_Arduino_MQTT_Libraries.md)<br>
Using PubSubClient and other Arduino MQTT libraries

[64. **Raspberry Pi MQTT Gateway**](docs/64_Raspberry_Pi_MQTT_Gateway.md)<br>
Building edge gateways with Raspberry Pi for protocol bridging

[65. **LoRaWAN to MQTT Bridge**](docs/65_LoRaWAN_to_MQTT_Bridge.md)<br>
Connecting long-range wireless sensors to MQTT infrastructure

## Cloud Integration

[66. **AWS IoT Core MQTT**](docs/66_AWS_IoT_Core_MQTT.md)<br>
Integrating with AWS IoT using MQTT with device shadows and rules

[67. **Azure IoT Hub MQTT**](docs/67_Azure_IoT_Hub_MQTT.md)<br>
Connecting devices to Azure IoT Hub via MQTT protocol

[68. **Google Cloud IoT Core**](docs/68_Google_Cloud_IoT_Core.md)<br>
Using MQTT bridge for Google Cloud Platform IoT services

[69. **IBM Watson IoT Platform**](docs/69_IBM_Watson_IoT_Platform.md)<br>
MQTT connectivity with IBM Watson for device management

[70. **Multi-Cloud MQTT Strategy**](docs/70_Multi_Cloud_MQTT_Strategy.md)<br>
Architecting vendor-agnostic MQTT solutions across cloud providers

## Protocol Extensions and Variants

[71. **MQTT over WebSockets**](docs/71_MQTT_over_WebSockets.md)<br>
Running MQTT in browsers and through firewalls using WebSocket transport

[72. **MQTT-RPC Protocol**](docs/72_MQTT_RPC_Protocol.md)<br>
Remote procedure call patterns and conventions over MQTT

[73. **Sparkplug B Specification**](docs/73_Sparkplug_B_Specification.md)<br>
Industrial IoT payload specification for MQTT in SCADA/ICS systems

[74. **Homie Convention**](docs/74_Homie_Convention.md)<br>
Standardized topic structure for home automation and IoT devices

[75. **MQTT v3.1.1 vs v5.0 Migration**](docs/75_MQTT_v3_1_1_vs_v5_0_Migration.md)<br>
Upgrading from MQTT 3.1.1 to 5.0 and compatibility considerations

## Advanced Security

[76. **End-to-End Encryption**](docs/76_End_to_End_Encryption.md)<br>
Implementing payload encryption independent of transport security

[77. **Key Rotation and Management**](docs/77_Key_Rotation_and_Management.md)<br>
Managing cryptographic keys in production MQTT deployments

[78. **Intrusion Detection**](docs/78_Intrusion_Detection.md)<br>
Monitoring MQTT traffic for security threats and anomalies

[79. **DDoS Protection**](docs/79_DDoS_Protection.md)<br>
Mitigating distributed denial of service attacks on MQTT brokers

[80. **Secure Boot and Attestation**](docs/80_Secure_Boot_and_Attestation.md)<br>
Device identity verification and trusted execution environments

## Data Processing

[81. **Stream Processing with MQTT**](docs/81_Stream_Processing_with_MQTT.md)<br>
Real-time data processing using Kafka, Flink, or Spark with MQTT

[82. **Rule Engines and Automation**](docs/82_Rule_Engines_and_Automation.md)<br>
Implementing broker-side rules for automated actions and routing

[83. **Message Transformation**](docs/83_Message_Transformation.md)<br>
Converting message formats, protocols, and data enrichment

[84. **Time-Series Database Integration**](docs/84_Time_Series_Database_Integration.md)<br>
Storing MQTT data in InfluxDB, TimescaleDB, or Prometheus

[85. **Analytics and Machine Learning**](docs/85_Analytics_and_Machine_Learning.md)<br>
Applying ML models to MQTT streams for predictive analytics

## Mobile and Web Applications

[86. **Mobile MQTT Clients**](docs/86_Mobile_MQTT_Clients.md)<br>
Building iOS and Android apps with MQTT connectivity

[87. **React/Angular MQTT Integration**](docs/87_React_Angular_MQTT_Integration.md)<br>
Web application development with MQTT over WebSockets

[88. **Progressive Web Apps with MQTT**](docs/88_Progressive_Web_Apps_with_MQTT.md)<br>
Building offline-capable PWAs using MQTT for sync

[89. **Push Notification Systems**](docs/89_Push_Notification_Systems.md)<br>
Implementing push notifications using MQTT as transport

[90. **Real-Time Dashboards**](docs/90_Real_Time_Dashboards.md)<br>
Creating live monitoring dashboards with MQTT data feeds

## Specialized Use Cases

[91. **Smart Home Automation**](docs/91_Smart_Home_Automation.md)<br>
Home automation architectures using MQTT with Home Assistant and OpenHAB

[92. **Vehicle Telematics**](docs/92_Vehicle_Telematics.md)<br>
Connected car data collection and fleet management via MQTT

[93. **Industrial Automation (IIoT)**](docs/93_Industrial_Automation_IIoT.md)<br>
Manufacturing and process control using MQTT in Industry 4.0

[94. **Healthcare and Medical Devices**](docs/94_Healthcare_and_Medical_Devices.md)<br>
Remote patient monitoring and medical IoT with MQTT compliance

[95. **Smart City Infrastructure**](docs/95_Smart_City_Infrastructure.md)<br>
Traffic management, utilities, and civic IoT using MQTT

## Optimization and Troubleshooting

[96. **Network Topology Optimization**](docs/96_Network_Topology_Optimization.md)<br>
Designing efficient MQTT network architectures for various scenarios

[97. **Bandwidth Optimization**](docs/97_Bandwidth_Optimization.md)<br>
Minimizing data usage in bandwidth-constrained environments

[98. **Battery Life Optimization**](docs/98_Battery_Life_Optimization.md)<br>
Power-saving strategies for battery-operated MQTT devices

[99. **Troubleshooting Common Issues**](docs/99_Troubleshooting_Common_Issues.md)<br>
Diagnosing connection failures, message loss, and performance problems

[100. **Future of MQTT Protocol**](docs/100_Future_of_MQTT_Protocol.md)<br>
Upcoming features, standardization efforts, and protocol evolution