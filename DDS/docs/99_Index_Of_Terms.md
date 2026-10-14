## Index of core DDS terms

### 1. The Structural Hierarchy (The "Container" Model)

This diagram shows how entities are nested.

```ascii
+-----------------------------------------------------------------------+
|  DOMAIN (Logical Network Boundary)                                    |
|  (Defined by Domain ID: 0, 1, 2...)                                   |
|                                                                       |
|  +-----------------------------------------------------------------+  |
|  |  PARTICIPANT                                                    |  |
|  |  (Root Entity in this Domain)                                   |  |
|  |                                                                 |  |
|  |  +---------------------+     +---------------------+            |  |
|  |  | PUBLISHER           |     | SUBSCRIBER          |            |  |
|  |  | (QoS: Partition,    |     | (QoS: Partition,    |            |  |
|  |  |  Reliability)       |     |  Reliability)       |            |  |
|  |  |                     |     |                     |            |  |
|  |  | +-----------------+ |     | +-----------------+ |            |  |
|  |  | | DATAWRITER      | |     | | DATAREADER      | |            |  |
|  |  | | (Writes Samples)| |     | | (Reads Samples) | |            |  |
|  |  | +-----------------+ |     | +-----------------+ |            |  |
|  |  +---------------------+     +---------------------+            |  |
|  +-----------------------------------------------------------------+  |
+-----------------------------------------------------------------------+
```

| Term | Description | Key Concept |
| :--- | :--- | :--- |
| **Domain ID** | A logical network boundary. Participants in different Domain IDs cannot discover each other. | Isolation level. |
| **Participant** | The root entity within a Domain. It manages the lifecycle of Publishers/Subscriber within that domain. | The "Node" or "Agent". |
| **Publisher** | A high-level entity that manages one or more DataWriters. Used for grouping writers with similar QoS. | Grouping abstraction. |
| **Subscriber** | A high-level entity that manages one or more DataReaders. Used for grouping readers with similar QoS. | Grouping abstraction. |
| **DataWriter** | The low-level object responsible for writing data to the network. One per Topic per Publisher. | Actual sender handle. |
| **DataReader** | The low-level object responsible for reading data from the network. One per Topic per Subscriber. | Actual receiver handle. |

---

### 2. The Data Model (Topic, Key, Instance)

This diagram shows how data samples relate to Topics and Instances.

```ascii
+-----------------------------------------------------------------------+
|  TOPIC: "SensorData" (Schema Definition)                              |
|  Key: ["SensorID"]                                                    |
|                                                                       |
|  +---------------------+     +---------------------+                  |
|  | INSTANCE "A"        |     | INSTANCE "B"        |                  |
|  | (Key: SensorID=101) |     | (Key: SensorID=102) |                  |
|  |                     |     |                     |                  |
|  | +-----------------+ |     | +-----------------+ |                  |
|  | | Sample: Temp=20 | |     | | Sample: Temp=25 | |                  |
|  | +-----------------+ |     | +-----------------+ |                  |
|  |                     |     |                     |                  |
|  | +-----------------+ |     |                     |                  |
|  | | Sample: Temp=21 | |     |                     |                  |
|  | +-----------------+ |     |                     |                  |
|  +---------------------+     +---------------------+                  |
+-----------------------------------------------------------------------+
```

| Term | Description | Key Concept |
| :--- | :--- | :--- |
| **Topic** | The logical name and data type (schema) of the data. Defines *what* is communicated. | The "Message Type". |
| **Key** | The field(s) in the data that uniquely identify an **Instance**. Defined in `DDS_KEYED_TOPIC_QOS`. | The Primary Key. |
| **Instance** | A logical entity representing a specific real-world object, identified by its **Key** value. Multiple samples update the same Instance. | The "Entity Identity". |
| **Sample** | A single snapshot of data sent by a DataWriter. Contains the payload and metadata (Timestamp, Key, etc.). | The "Message". |

---

### 3. Filtering & Matching (Partition & QoS)

This diagram shows how Publishers and Subscribers match based on attributes.

```ascii
+-----------------------------------------------------------------------+
|  MATCHING LOGIC                                                       |
|                                                                       |
|  Publisher:                                                           |
|    - Topic: "Temp"                                                    |
|    - Partition: "Lab_A"                                               |
|    - Reliability: BestEffort                                          |
|                                                                       |
|  Subscriber:                                                          |
|    - Topic: "Temp"                                                    |
|    - Partition: "Lab_A"  <--- Match? YES (Exact Match)                |
|    - Partition: "Lab_B"  <--- Match? NO  (Mismatch)                   |
|    - Partition: ""     <--- Match? YES  (Empty matches all)           |
|    - Reliability: Reliable <--- Match? YES (Compatible)               |
+-----------------------------------------------------------------------+
```

| Term | Description | Key Concept |
| :--- | :--- | :--- |
| **Partition** | A string tag applied to Publishers/Subscribers. Acts as a logical filter. Only Publishers and Subscribers with the *same* non-empty partition match. | Logical Grouping / Firewall. |
| **QoS (Quality of Service)** | A set of policies (Reliability, Durability, History, etc.) that define *how* data is delivered. | Performance & Guarantee config. |
| **Reliability** | A QoS policy. **Best-Effort**: No guarantee of delivery (UDP-like). **Reliable**: Guaranteed delivery (TCP-like), uses acknowledgments. | Delivery Guarantee. |
| **Durability** | A QoS policy. **Transient**: New subscribers get old data. **Persistent**: Data survives across runs (stored in DB). | Data Persistence. |
| **History** | A QoS policy. Defines how many samples are kept in the cache. **Keep-Last**: Only latest N samples. **Keep-All**: All samples. | Memory Management. |

---

### 4. Lifecycle & Discovery

| Term | Description | Key Concept |
| :--- | :--- | :--- |
| **Discovery** | The process by which Publishers and Subscribers find each other. Cyclone DDS uses **Liveliness** to detect participants. | Auto-configuration. |
| **Liveliness** | A mechanism to detect if a Publisher/Subscriber is still active. If a writer stops sending "heartbeats," readers know it's gone. | Heartbeat/Alive detection. |
| **Last-Writer-Wins** | A concurrency model. If multiple writers write to the same Instance, the last one to write determines the value seen by readers. | Conflict Resolution. |
| **Ownership** | A QoS policy. Defines which DataWriter has the right to update a specific Instance. Prevents race conditions. | Exclusive Control. |
| **Disposed** | A state of an Instance. Indicates that the real-world entity no longer exists (e.g., a sensor was unplugged). | Entity Removal. |

### Quick Reference: The "Hello World" Mapping

| DDS Concept | Real-World Analogy |
| :--- | :--- |
| **Domain** | A specific office building. |
| **Participant** | A person in the building. |
| **Topic** | A type of memo (e.g., "Sales Report"). |
| **Key** | The Client Name on the memo. |
| **Instance** | A specific Client (e.g., "Acme Corp"). |
| **Partition** | A department tag (e.g., "Finance"). |
| **Sample** | The specific content of the memo sent at 9:00 AM. |
| **DataWriter** | The person sending the memo. |
| **DataReader** | The person reading the memo. |