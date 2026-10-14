# Comprehensive ASCII diagram illustrating the logical hierarchy and interaction flow in Cyclone DDS.

### Key Concepts Illustrated:
1. **Domain ID**: The highest-level isolation layer. Only nodes in the **same Domain ID** can communicate.
2. **Partitions**: Logical sub-groups within a Domain. A Publisher or Subscriber must explicitly specify a partition. By default, it's an empty string (`""`), which matches *all* partitions. A non-empty partition acts as a filter.
3. **Topics**: The abstract data type (schema).
4. **Publishers/Consumers (Subscribers)**: Entities that create/destroy data flow.
5. **DataWriters/DataReaders**: Low-level handles that bind to Publishers/Subscribers.
6. **Reliability/QoS**: Determines how data flows (e.g., best-effort vs. reliable).

```
+=========================================================================================+
|                             CYCLONE DDS: ENTITY HIERARCHY & INTERACTION                 |
+=========================================================================================+

+-----------------------+       +-----------------------+
|   DOMAIN 0            |       |   DOMAIN 1            |
|  (Isolation Boundary) |       |  (Isolation Boundary) |
+-----------------------+       +-----------------------+
|                       |       |                       |
|  +-----------------+  |       |  +-----------------+  |
|  | Partition: "A"  |  |       |  | Partition: "B"  |  |
|  |                 |  |       |  |                 |  |
|  |  +-----------+  |  |       |  |  +-----------+  |  |
|  |  | Publisher |  |  |       |  |  | Subscriber|  |  |
|  |  |           |  |  |       |  |  |           |  |  |
|  |  | DataWriter|--+--|-------+--|----DataReader|  |  | PARTITION MISMATCH
|  |  | (Topic: T)|  |  |       |  |  | (Topic: T)|  |  | Result: NO MATCH - Data is dropped.
|  |  +-----------+  |  |       |  |  +-----------+  |  |
|  +-----------------+  |       |  +-----------------+  |
|                       |       |                       |
|  +-----------------+  |       |  +-----------------+  |
|  | Partition: "A"  |  |       |  | Partition: "A"  |  |
|  |                 |  |       |  |                 |  |
|  |  +-----------+  |  |       |  |  +-----------+  |  |
|  |  | Subscriber|  |  |       |  |  | Publisher |  |  |
|  |  |           |  |  |       |  |  |           |  |  |
|  |  | DataReader|--+--|-------+--|----DataWriter|  |  | MATCHING BY PARTITION ("A")
|  |  | (Topic: T)|  |  |       |  |  | (Topic: T)|  |  | Result: MATCH - Data flows.
|  |  +-----------+  |  |       |  |  +-----------+  |  |
|  +-----------------+  |       |  +-----------------+  |
|                       |       |                       |
|  +-----------------+  |       |  +-----------------+  |
|  | Partition: "All"|  |       |  | Partition: "All"|  |
|  | (Default "")    |  |       |  | (Default "")    |  |
|  |                 |  |       |  |                 |  |
|  |  +-----------+  |  |       |  |  +-----------+  |  |
|  |  | Publisher |  |  |       |  |  | Subscriber|  |  |
|  |  |           |  |  |       |  |  |           |  |  |
|  |  | DataWriter|--+--|-------+--|----DataReader|  |  | DEFAULT PARTITION ("") MATCHES ALL
|  |  |(Topic: T2)|  |  |       |  |  |(Topic: T2)|  |  | Result: MATCH - Data flows to any subscriber,
|  |  +-----------+  |  |       |  |  +-----------+  |  | regardless of its partition.
|  +-----------------+  |       |  +-----------------+  |
+-----------------------+       +-----------------------+

=========================================================================================
INTERACTION FLOW EXAMPLES
=========================================================================================

1. MATCHING BY PARTITION ("A")
   +---------------------------------------------------------------+
   | Publisher (Part: "A", Topic: T)                               |
   |   |                                                           |
   |   |  DataWriter sends data on Topic T                         |
   |   |  Filters: Partition == "A"                                |
   |   v                                                           |
   |  [Network/DDS Middleware]                                     |
   |   ^                                                           |
   |   |  DataReader receives only if its Partition matches "A"    |
   |   |  Subscriber (Part: "A", Topic: T)                         |
   +---------------------------------------------------------------+
   * Result: MATCH - Data flows.

2. PARTITION MISMATCH
   +---------------------------------------------------------------+
   | Publisher (Part: "A", Topic: T)                               |
   |   |                                                           |
   |   |  DataWriter sends data on Topic T                         |
   |   |  Filters: Partition == "A"                                |
   |   v                                                           |
   |  [Network/DDS Middleware]                                     |
   |   ^                                                           |
   |   |  DataReader IGNORES because its Partition is "B"          |
   |   |  Subscriber (Part: "B", Topic: T)                         |
   +---------------------------------------------------------------+
   * Result: NO MATCH - Data is dropped.

3. DEFAULT PARTITION ("") MATCHES ALL
   +---------------------------------------------------------------+
   | Publisher (Part: "", Topic: T)                                |
   |   |                                                           |
   |   |  DataWriter sends data on Topic T                         |
   |   |  Filters: Partition == "" (matches any)                   |
   |   v                                                           |
   |  [Network/DDS Middleware]                                     |
   |   ^                                                           |
   |   |  DataReader matches because "" matches any partition      |
   |   |  Subscriber (Part: "X", Topic: T)                         |
   |   |  OR                                                       |
   |   |  Subscriber (Part: "", Topic: T)                          |
   +---------------------------------------------------------------+
   * Result: MATCH - Data flows to any subscriber, regardless of its partition.

4. DOMAIN ISOLATION
   +---------------------------------------------------------------+
   | Domain 0: Publisher (Topic: T)                                |
   |   |                                                           |
   |   |  DataWriter sends data                                    |
   |   v                                                           |
   |  [DDS Middleware]                                             |
   |   ^                                                           |
   |   |  DataReader in Domain 1 IGNORES this data                 |
   |   |  Subscriber (Domain 1, Topic: T)                          |
   +---------------------------------------------------------------+
   * Result: NO MATCH - Different Domain IDs prevent discovery.

=========================================================================================
ENTITY LAYERING (Inside a Node)
=========================================================================================

+-----------------------------------------------------------------------+
|  DDS Node                                                             |
|                                                                       |
|  +---------------------+     +---------------------+                  |
|  |  Participant        |     |  Participant        |                  |
|  |  (Domain 0)         |     |  (Domain 1)         |                  |
|  |                     |     |                     |                  |
|  |  +---------------+  |     |  +---------------+  |                  |
|  |  | Publisher     |  |     |  | Subscriber    |  |                  |
|  |  | (Topic: T)    |  |     |  | (Topic: T)    |  |                  |
|  |  |               |  |     |  |               |  |                  |
|  |  | DataWriter    |  |     |  | DataReader    |  |                  |
|  |  |               |  |     |  |               |  |                  |
|  |  +---------------+  |     |  +---------------+  |                  |
|  |                     |     |                     |                  |
|  +---------------------+     +---------------------+                  |
|                                                                       |
+-----------------------------------------------------------------------+

* Participants are the root entities within a Domain.
* Publishers/Consumers are created within Participants.
* DataWriters/DataReaders are the actual objects that send/receive data.
* Partition is a QoS (Quality of Service) property applied to Publishers/Subscribers.
* Domain ID is a property of the Participant.
```

### Summary of Interactions:

1. **Discovery Phase**:
   - When a Publisher is created, it announces its existence (Topic, Partition, QoS) to the Domain.
   - Subscribers listen for announcements that match their Topic and Partition.

2. **Connection Phase**:
   - If a Subscriber finds a matching Publisher (same Domain, same Topic, compatible Partition), a connection is established.
   - Partition Matching Rule:
     - If Publisher’s partition is `""`, it matches any Subscriber.
     - If Subscriber’s partition is `""`, it matches any Publisher.
     - If both are non-empty, they must be **identical**.

3. **Data Flow Phase**:
   - `DataWriter` sends serialized data.
   - `DataWriter` checks the Subscriber’s QoS (e.g., Reliability: Best-Effort or Reliable).
   - `DataReader` receives data and deserializes it into the application.

This diagram and explanation cover the core logical subdivisions and their interaction in Cyclone DDS.