## DDS Protocol — Senior Engineer Knowledge Map

## Core DDS Concepts
[01. **What is DDS**](docs/01_What_Is_DDS.md)<br>
Data-centric publish-subscribe paradigm, decoupling in time/space/flow, comparison to broker-based messaging (Kafka, MQTT, AMQP).

[02. **Domain and DomainParticipant**](docs/02_Domain_And_DomainParticipant.md)<br>
Domain isolation via Domain ID, DomainParticipant as the entry point entity and its lifecycle.

[03. **Topics and Data Types**](docs/03_Topics_And_Data_Types.md)<br>
Topic as the unit of information exchange, binding a name to a type, topic types (Topic, ContentFilteredTopic, MultiTopic).

[04. **Publishers and Subscribers**](docs/04_Publishers_And_Subscribers.md)<br>
Container entities that group DataWriters/DataReaders and coordinate QoS at a higher level.

[05. **DataWriters and DataReaders**](docs/05_DataWriters_And_DataReaders.md)<br>
The actual read/write endpoints, listeners, WaitSets, and status conditions.

## Quality of Service (QoS)
[06. **QoS Framework Overview**](docs/06_QoS_Framework_Overview.md)<br>
How QoS policies compose, where they apply (entity level), and the request/offered compatibility model.

[07. **Reliability QoS**](docs/07_Reliability_QoS.md)<br>
BEST_EFFORT vs RELIABLE delivery semantics and their protocol-level implications.

[08. **Durability QoS**](docs/08_Durability_QoS.md)<br>
VOLATILE, TRANSIENT_LOCAL, TRANSIENT, PERSISTENT and late-joiner data delivery.

[09. **History and Resource Limits QoS**](docs/09_History_And_Resource_Limits_QoS.md)<br>
KEEP_LAST vs KEEP_ALL, depth tuning, and bounding memory/queue usage.

[10. **Deadline QoS**](docs/10_Deadline_QoS.md)<br>
Contracted update periods and missed-deadline detection between writer and reader.

[11. **Liveliness QoS**](docs/11_Liveliness_QoS.md)<br>
AUTOMATIC, MANUAL_BY_PARTICIPANT, MANUAL_BY_TOPIC and failure/timeout detection.

[12. **Ownership and Ownership Strength QoS**](docs/12_Ownership_And_Ownership_Strength_QoS.md)<br>
SHARED vs EXCLUSIVE ownership models for redundant publishers.

[13. **Partition QoS**](docs/13_Partition_QoS.md)<br>
Logical subdivision of a domain for matching control without extra topics.

[14. **Lifespan and Time-Based Filter QoS**](docs/14_Lifespan_And_Time_Based_Filter_QoS.md)<br>
Expiring stale samples and throttling reader-side update rates.

[15. **QoS Compatibility and Matching Rules**](docs/15_QoS_Compatibility_And_Matching_Rules.md)<br>
Request-vs-offered semantics that determine whether a writer/reader pair connects.

## RTPS Wire Protocol
[16. **RTPS Protocol Overview**](docs/16_RTPS_Protocol_Overview.md)<br>
Real-Time Publish-Subscribe as the standardized interoperability wire protocol underlying DDS.

[17. **RTPS Message and Submessage Structure**](docs/17_RTPS_Message_And_Submessage_Structure.md)<br>
Header/submessage layout, DATA, HEARTBEAT, ACKNACK, GAP, INFO_TS submessages.

[18. **Sequence Numbers, Heartbeats and ACKNACK**](docs/18_Sequence_Numbers_Heartbeats_And_ACKNACK.md)<br>
Reliable delivery mechanics: gap detection, retransmission, and flow control.

[19. **Entity IDs and GUIDs**](docs/19_Entity_IDs_And_GUIDs.md)<br>
Global unique identification of participants, writers, and readers on the wire.

## Discovery
[20. **Simple Participant Discovery Protocol (SPDP)**](docs/20_Simple_Participant_Discovery_Protocol.md)<br>
How participants find each other via multicast/unicast announcements.

[21. **Simple Endpoint Discovery Protocol (SEDP)**](docs/21_Simple_Endpoint_Discovery_Protocol.md)<br>
How writers and readers exchange topic/type/QoS metadata to match.

[22. **Discovery Scalability and Tuning**](docs/22_Discovery_Scalability_And_Tuning.md)<br>
Managing discovery traffic growth (O(n²)) via initial peers, discovery servers, and static discovery.

## Data Modeling & Type System
[23. **IDL and Data Type Definition**](docs/23_IDL_And_Data_Type_Definition.md)<br>
Interface Definition Language for structs, enums, unions, and code generation.

[24. **Keys and Instances**](docs/24_Keys_And_Instances.md)<br>
Key fields, instance identity, instance states (ALIVE/DISPOSED/NO_WRITERS), and instance handles.

[25. **Serialization (CDR)**](docs/25_Serialization_CDR.md)<br>
Common Data Representation encoding rules and endianness handling on the wire.

[26. **Extensible Types (XTypes)**](docs/26_Extensible_Types_XTypes.md)<br>
Type evolution, mutable/appendable/final extensibility, and type compatibility checking.

## Transport & Performance
[27. **Transport Configuration**](docs/27_Transport_Configuration.md)<br>
UDPv4/v6, TCP, and Shared Memory transports and when to use each.

[28. **Multicast vs Unicast Delivery**](docs/28_Multicast_Vs_Unicast_Delivery.md)<br>
Trade-offs for fan-out efficiency vs network/switch constraints.

[29. **Performance Tuning and Batching**](docs/29_Performance_Tuning_And_Batching.md)<br>
Sample batching, send queues, thread pools, and latency vs throughput trade-offs.

[30. **WAN and Routing (Routing/Gateway Services)**](docs/30_WAN_And_Routing_Services.md)<br>
Bridging domains across WAN links, NAT traversal, and topic routing/filtering.

## Security
[31. **DDS Security Overview**](docs/31_DDS_Security_Overview.md)<br>
The pluggable security model built on Authentication, Access Control, and Cryptographic plugins.

[32. **Authentication and Access Control**](docs/32_Authentication_And_Access_Control.md)<br>
Identity CA, permissions documents, and governance/permissions XML.

[33. **Data Encryption and Signing**](docs/33_Data_Encryption_And_Signing.md)<br>
Per-topic encryption, message signing, and key exchange overhead.

## Ecosystem, Interoperability & Operations
[34. **Vendor Interoperability**](docs/34_Vendor_Interoperability.md)<br>
Cross-vendor RTPS compliance (RTI Connext, Fast DDS, CycloneDDS, OpenDDS) and common pitfalls.

[35. **Content-Filtered and Multi-Topics**](docs/35_Content_Filtered_And_Multi_Topics.md)<br>
Server-side filtering to reduce bandwidth and reader-side load.

[36. **Monitoring, Debugging and Tooling**](docs/36_Monitoring_Debugging_And_Tooling.md)<br>
Wireshark RTPS dissection, admin consoles, and built-in topics for introspection.

[37. **DDS in ROS 2**](docs/37_DDS_In_ROS2.md)<br>
How ROS 2 middleware (rmw) layers on top of DDS implementations, and its QoS mapping.

[38. **Failover, Redundancy and Fault Tolerance Patterns**](docs/38_Failover_Redundancy_And_Fault_Tolerance.md)<br>
Designing resilient systems using ownership strength, liveliness, and multi-writer redundancy.

[39. **DDS Usecases Diagrams**](docs/39_DDS_Use_Cases_ASCII_Diagrams.md)<br>
A collection of ASCII diagrams illustrating how DDS is used in practice, from core mechanics to industry-specific architectures.

