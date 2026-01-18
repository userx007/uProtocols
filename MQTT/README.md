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