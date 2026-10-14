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

---

### A Mental Model: DDS Sits "Above" RTPS

┌─────────────────────────────────────────────┐
│  Application code                           │
│  (write(), take(), QoS objects, listeners)  │
├─────────────────────────────────────────────┤
│  DDS API layer                              │
│  (DomainParticipant, Topic, DataWriter,...) │
├─────────────────────────────────────────────┤
│  RTPS protocol layer                        │
│  (Participants, Writers, Readers, GUIDs,    │
│   discovery, reliability, submessages)      │
├─────────────────────────────────────────────┤
│  Transport layer                            │
│  (UDP/IP multicast+unicast, TCP, Shared     │
│   Memory — topic 27)                        │
└─────────────────────────────────────────────┘


### The Big Picture: One UDP Packet, One RTPS Message, Many Submessages

RTPS Message
 ├── RTPS Header (fixed, 20 bytes)
 └── One or more Submessages, back to back
      ├── Submessage 1 (e.g., INFO_TS)
      ├── Submessage 2 (e.g., DATA)
      ├── Submessage 3 (e.g., HEARTBEAT)
      └── ...

#### RTPS Header

The fixed 20-byte header appears once per message, identifying the protocol and
sending participant:

| Field | Size | Purpose |
|---|---|---|
| `protocol` | 4 bytes | Literal `"RTPS"` — identifies this as an RTPS packet. |
| `protocolVersion` | 2 bytes | RTPS protocol version (e.g., 2.3, 2.5). |
| `vendorId` | 2 bytes | Identifies which vendor's implementation sent this (RTI, eProsima, Eclipse, etc.) — used for vendor-specific interoperability handling if needed. |
| `guidPrefix` | 12 bytes | The **GUID prefix** of the sending Participant — combined with per-entity Entity IDs in submessages to form full GUIDs (topic 19). |

```
Submessage
 ├── submessageId    (1 byte)  — identifies the kind (DATA, HEARTBEAT, ACKNACK, GAP, INFO_TS, ...)
 ├── flags           (1 byte)  — kind-specific flags (endianness, presence of optional fields, etc.)
 ├── submessageLength (2 bytes) — length of this submessage's content
 └── submessage-specific content
```

Here is a comprehensive, single-page ASCII diagram illustrating the complete hierarchy and data flow in Cyclone DDS, from the **Domain** level down to the **Instance** level.

### 🌌 The Complete DDS Hierarchy & Data Flow

```ascii
========================================================================================
                      CYCLONE DDS: ENTIRE LOGICAL ARCHITECTURE
========================================================================================

+------------------------------------------------------------------------------------+
|  DOMAIN 0 (Logical Network Boundary)                                               |
|  (Only Participants in the same Domain ID communicate)                             |
|                                                                                    |
|  +--------------------------------------------------------------------------+      |
|  |  PARTICIPANT 0                                                           |      |
|  |  (The root object representing this process/node)                        |      |
|  |                                                                          |      |
|  |  +--------------------------------------------------------------------+  |      |
|  |  |  PUBLISHER (Logical Group for Writers)                             |  |      |
|  |  |  QoS: Reliability=BestEffort, Partition="Team_A"                   |  |      |
|  |  |                                                                    |  |      |
|  |  |  +------------------------------------------------------------+    |  |      |
|  |  |  |  DATAWRITER 1                                              |    |  |      |
|  |  |  |  Binds to: Topic "SensorData"                              |    |  |      |
|  |  |  |  Key Field: "DeviceID"                                     |    |  |      |
|  |  |  |                                                            |    |  |      |
|  |  |  |  +----------------------------------------------------+    |    |  |      |
|  |  |  |  |  NETWORK TRANSPORT (UDP/TCP)                       |    |    |  |      |
|  |  |  |  |  (Sends serialized bytes over the network)         |    |    |  |      |
|  |  |  |  +----------------------------------------------------+    |    |  |      |
|  |  |  +------------------------------------------------------------+    |  |      |
|  |  |                                                                    |  |      |
|  |  |  +------------------------------------------------------------+    |  |      |
|  |  |  |  DATAWRITER 2                                              |    |  |      |
|  |  |  |  Binds to: Topic "CommandData"                             |    |  |      |
|  |  |  |  Key Field: "None" (Non-keyed)                             |    |  |      |
|  |  |  +------------------------------------------------------------+    |  |      |
|  |  +--------------------------------------------------------------------+  |      |
|  |                                                                          |      |
|  |  +--------------------------------------------------------------------+  |      |
|  |  |  SUBSCRIBER (Logical Group for Readers)                            |  |      |
|  |  |  QoS: Reliability=BestEffort, Partition="Team_A"                   |  |      |
|  |  |                                                                    |  |      |
|  |  |  +------------------------------------------------------------+    |  |      |
|  |  |  |  DATAREADER 1                                              |    |  |      |
|  |  |  |  Binds to: Topic "SensorData"                              |    |  |      |
|  |  |  |  Matches Partition "Team_A" & Key "DeviceID"               |    |  |      |
|  |  |  |                                                            |    |  |      |
|  |  |  |  +----------------------------------------------------+    |    |  |      |
|  |  |  |  |  LOCAL CACHE (History/Instances)                   |    |    |  |      |
|  |  |  |  |  Holds the last samples for each Instance          |    |    |  |      |
|  |  |  |  +----------------------------------------------------+    |    |  |      |
|  |  |  |                                                            |    |  |      |
|  |  |  |  +----------------------------------------------------+    |    |  |      |
|  |  |  |  |  INSTANCE CACHE (Keyed Data)                       |    |    |  |      |
|  |  |  |  |                                                    |    |    |  |      |
|  |  |  |  |  +-----------------+     +---------------------+   |    |    |  |      |
|  |  |  |  |  | INSTANCE "101"  |     | INSTANCE "102"      |   |    |    |  |      |
|  |  |  |  |  | (DeviceID=101)  |     | (DeviceID=102)      |   |    |    |  |      |
|  |  |  |  |  |                 |     |                     |   |    |    |  |      |
|  |  |  |  |  | [Sample: Temp   |     | [Sample: Temp       |   |    |    |  |      |
|  |  |  |  |  |   20°C]         |     |   25°C]             |   |    |    |  |      |
|  |  |  |  |  |                 |     |                     |   |    |    |  |      |
|  |  |  |  |  | [Sample: Humid  |     | [Sample: Humid      |   |    |    |  |      |
|  |  |  |  |  |   50%]          |     |   60%]              |   |    |    |  |      |
|  |  |  |  |  +-----------------+     +---------------------+   |    |    |  |      |
|  |  |  |  +----------------------------------------------------+    |    |  |      |
|  |  |  +------------------------------------------------------------+    |  |      |
|  |  +--------------------------------------------------------------------+  |      |
|  +--------------------------------------------------------------------------+      |
+------------------------------------------------------------------------------------+
```

---

### 🔍 Detailed Breakdown of the Diagram

#### 1. **Domain (Top Level)**
*   **Role**: The highest level of isolation.
*   **Concept**: Participants in Domain 0 cannot see or talk to Participants in Domain 1.
*   **ASCII Location**: The outermost box.

#### 2. **Participant (The Node)**
*   **Role**: Represents the application process. It manages the lifecycle of all entities created within it.
*   **Concept**: You create one Participant per process. It owns the Domain ID.
*   **ASCII Location**: The large inner box labeled `PARTICIPANT 0`.

#### 3. **Publisher / Subscriber (Logical Groups)**
*   **Role**: High-level abstractions to group multiple DataWriters/DataReaders.
*   **Concept**:
    *   **Publisher**: Groups writers that share similar QoS (like Reliability).
    *   **Subscriber**: Groups readers that share similar QoS.
    *   *Note*: A single Topic usually has 1 Publisher/Subscriber pair in simple cases, but complex systems may have multiple writers per topic for redundancy.
*   **ASCII Location**: The boxes labeled `PUBLISHER` and `SUBSCRIBER` inside the Participant.

#### 4. **DataWriter / DataReader (The Actual I/O)**
*   **Role**: The low-level objects that perform the serialization/deserialization and network I/O.
*   **Concept**:
    *   **DataWriter**: Bound to a **Topic**. It takes your C/C++ struct and sends it.
    *   **DataReader**: Bound to a **Topic**. It receives bytes and deserializes them into your struct.
*   **ASCII Location**: The boxes labeled `DATAWRITER 1` and `DATAREADER 1`.

#### 5. **Topic (The Schema)**
*   **Role**: Defines the name and data type (struct/class) of the data.
*   **Concept**: The "Contract" between writer and reader. Both must agree on the Topic Name and Data Type.
*   **ASCII Location**: Defined inside the Writer/Reader boxes (`Topic "SensorData"`).

#### 6. **Instance (The Identity)**
*   **Role**: Represents a specific real-world entity identified by a **Key**.
*   **Concept**:
    *   If the Topic has a Key (e.g., `DeviceID`), each unique Key value creates a **new Instance**.
    *   The DataReader maintains a separate "cache" or "state" for each Instance.
    *   *Non-Keyed Topics*: Have only one Instance (often called the "anonymous instance").
*   **ASCII Location**: The detailed breakdown inside `DATAREADER 1` showing `INSTANCE "101"` and `INSTANCE "102"`.

---

### 🔄 Data Flow Arrow (Implicit)

1.  **Application Code** calls `write()` on **DataWriter**.
2.  **DataWriter** serializes the struct into bytes.
3.  **Network** transports bytes to **DataReader**.
4.  **DataReader** deserializes bytes.
5.  **DataReader** updates the **Instance Cache** (based on the Key).
6.  **Application Code** calls `read()` or `take()` on **DataReader** to get the data for the specific **Instance**.

### 💡 Key Takeaways from this Diagram

1.  **Hierarchical**: Domain > Participant > Publisher/Subscriber > DataWriter/DataReader > Topic > Instance.
2.  **Loose Coupling**: The Publisher doesn't know about the Subscriber. The DataWriter doesn't know about the DataReader. They only know the **Topic**.
3.  **Instance Isolation**: Data for different Keys (Instances) is stored and managed separately. This allows you to query "Give me data for Device 101" without caring about Device 102.
4.  **Partition Filtering**: The `Partition` attribute in the Publisher/Subscriber acts as a filter before the data even reaches the DataReader.