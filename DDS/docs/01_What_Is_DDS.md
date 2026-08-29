# 01. What is DDS

## Overview

**DDS (Data Distribution Service)** is an Object Management Group (OMG) standard for
**data-centric publish-subscribe (DCPS)** communication, originally published in 2004 and
still actively maintained (latest formal revision: DDS 1.4, alongside the companion
**DDS-RTPS** wire protocol standard that guarantees interoperability between vendor
implementations).

DDS was designed for systems that need:

- **Low latency** and **high throughput** (microseconds to low milliseconds)
- **Deterministic, real-time behavior**
- **No single point of failure** (no broker/server in the data path)
- **Fine-grained control** over delivery guarantees, per data flow

It is the dominant middleware in domains such as **defense, aerospace, autonomous
vehicles, industrial automation, robotics (ROS 2), medical devices, and financial trading
systems** — anywhere a broker-based queue would introduce unacceptable latency or a
single point of failure.

DDS is not a product — it's a **specification**. Multiple independent, interoperable
implementations exist: RTI Connext, eProsima Fast DDS, Eclipse Cyclone DDS, OpenDDS,
GurumDDS, and others. They interoperate at the wire level via **RTPS** (Real-Time
Publish-Subscribe protocol), covered in topic 16.

---

## The Data-Centric Publish-Subscribe Paradigm

Traditional publish-subscribe systems (message queues, brokers) are **message-centric**:
they move opaque payloads (bytes) from a producer to a consumer through an intermediary.
The middleware has no idea what's *inside* the message.

DDS is **data-centric**: the middleware understands the *structure* and *semantics* of the
data itself.

Key implications:

- Every piece of data is published under a **Topic**, which binds a **name** to a
  **strongly-typed data structure** (defined in IDL — see topic 23).
- DDS maintains a **global data space**, conceptually a distributed, virtual database of
  the *current* (and optionally historical) state of every topic instance in the domain.
- Readers don't just receive "a message" — they receive **samples of an instance's
  state**, and DDS tracks **instance identity via key fields** (topic 24), instance
  lifecycle (ALIVE / DISPOSED / NO_WRITERS), and staleness.
- Because the middleware understands the data, it can do things a message broker cannot:
  filter by content (topic 35), merge/query data (MultiTopic), detect missed updates
  (Deadline QoS), and manage per-instance ownership (Ownership QoS).

**Analogy:** a message-centric system is like a postal courier — it delivers sealed
envelopes and doesn't care what's inside. A data-centric system is like a shared,
continuously-updated whiteboard — every participant sees the current values of the
fields they care about, automatically kept in sync.

### Minimal conceptual example

```idl
// IDL: defines the *shape* of the data DDS understands and manages
struct VehiclePosition {
    @key long vehicle_id;   // key field -> defines instance identity
    double latitude;
    double longitude;
    double speed_kmh;
    long long timestamp;
};
```

```cpp
// Publisher side (pseudo-code, C++ DDS API)
DomainParticipant* participant = factory->create_participant(DOMAIN_ID);
Topic* topic = participant->create_topic("VehiclePosition", "VehiclePosition::Type");
Publisher* pub = participant->create_publisher();
DataWriter* writer = pub->create_datawriter(topic, qos);

VehiclePosition sample{.vehicle_id = 42, .latitude = 48.1, .longitude = 8.4,
                        .speed_kmh = 63.5, .timestamp = now()};
writer->write(sample);
```

```cpp
// Subscriber side — anywhere on the network, no broker involved
DataReader* reader = sub->create_datareader(topic, qos);
reader->set_listener(new MyListener()); // on_data_available callback fires
```

The publisher never knows (or cares) who is subscribed, how many subscribers exist, or
where they are. DDS handles discovery, matching, and delivery transparently.

---

## Decoupling in Time, Space, and Flow

DDS's core value proposition is often summarized as **three-way decoupling**:

### 1. Decoupling in Space
Publishers and subscribers do not need to know each other's network location, process,
or host. There is no broker address to configure — DDS entities discover each other
automatically via a **peer-to-peer discovery protocol** (topics 20–22). A publisher just
declares "I write Topic X"; any matching subscriber anywhere on the domain connects
automatically.

### 2. Decoupling in Time
Thanks to **Durability QoS** (topic 08), a subscriber that joins *after* data was
published can still receive relevant historical samples (`TRANSIENT_LOCAL`,
`TRANSIENT`, `PERSISTENT`). Publishers and subscribers do not need to be online
simultaneously — within the bounds of the configured durability/history policy.

### 3. Decoupling in Flow
Publishers and subscribers do not block each other. A slow subscriber does not slow down
a fast publisher (bounded by `RESOURCE_LIMITS` and `HISTORY` QoS, topic 09); each
subscriber can consume data at its own rate. There is no shared queue that couples
producer and consumer throughput together, unlike many broker-based systems.

Together, these three decouplings let DDS support **highly dynamic topologies**: nodes
can join, leave, restart, or fail independently, and the system self-heals without
reconfiguration.

---

## DDS vs. Broker-Based Messaging (Kafka, MQTT, AMQP)

This is one of the most common points of confusion for engineers coming from enterprise
messaging backgrounds. The fundamental architectural difference is **DDS has no broker**
— it is **peer-to-peer**. Kafka, MQTT, and AMQP all rely on a central (or clustered)
intermediary that sits in the data path.

| Aspect | **DDS** | **Kafka** | **MQTT** | **AMQP (e.g. RabbitMQ)** |
|---|---|---|---|---|
| Architecture | Peer-to-peer, brokerless | Broker cluster (log-based) | Broker (central) | Broker (central/clustered) |
| Data model | Data-centric, typed, key-based instances | Byte payload + offset log | Opaque byte payload | Opaque byte payload |
| Discovery | Automatic (dynamic, decentralized) | Manual broker/topic config | Manual broker config | Manual broker/exchange config |
| Latency | Microseconds–low ms (real-time capable) | Low ms–tens of ms (throughput optimized) | Low ms (lightweight) | Low–moderate ms |
| Delivery guarantees | Per-flow QoS (20+ policies: reliability, deadline, ownership, liveliness…) | At-least-once / exactly-once (config'd) | QoS 0/1/2 (3 levels) | At-most/least/exactly-once via acks |
| Persistence model | Optional, per-topic (Transient/Persistent QoS) | Durable log, replay by offset | Optional retained messages | Optional durable queues |
| Typical use case | Real-time control systems, avionics, robotics, SCADA | Event streaming, log aggregation, analytics pipelines | IoT telemetry, constrained devices | Enterprise integration, task queues |
| Single point of failure | None (no broker) | Broker cluster (mitigated by replication) | Broker | Broker (mitigated by clustering) |
| Scalability pattern | Scales via multicast + direct peer connections | Scales via partitions across brokers | Scales via broker clustering/bridging | Scales via broker clustering/federation |
| Wire protocol standard | RTPS (OMG standard, cross-vendor interoperable) | Kafka wire protocol (proprietary-ish, single ecosystem) | MQTT (OASIS standard) | AMQP 0-9-1 / 1.0 (standard) |

### When DDS wins
- Hard real-time or safety-critical control loops (e.g., flight control, autonomous
  driving perception/planning, industrial robot arms).
- Environments where a broker is an unacceptable single point of failure or added hop
  of latency.
- Systems needing rich, declarative QoS **per data flow** rather than per-broker/queue
  (e.g., one topic reliable + durable, another best-effort + volatile, in the same app).
- Embedded/edge systems needing to avoid the operational overhead of running and scaling
  a broker cluster.

### When Kafka/MQTT/AMQP win
- Large-scale event streaming, analytics, and log aggregation where durable replay by
  offset (Kafka) is the primary need.
- Massive numbers of intermittently-connected, resource-constrained devices (MQTT is
  purpose-built for this — e.g., battery-powered IoT sensors over cellular).
- Enterprise integration patterns: routing, transformation, work queues, RPC-style
  request/reply across heterogeneous systems (AMQP's strength).
- Teams that want the simpler, well-understood **operational model** of a managed broker
  service (many hosted Kafka/MQTT offerings exist; hosted DDS is rarer).

### A concrete mental model
- **Kafka** = a durable, ordered, replayable commit log you write to and read from.
- **MQTT** = a lightweight "fire it at the broker, broker fans it out" pattern for simple
  telemetry.
- **AMQP** = a flexible message-routing broker with exchanges, queues, and bindings.
- **DDS** = a shared, live, strongly-typed data space where the network *itself* handles
  matching, delivery guarantees, and instance lifecycle — no middleman process.

---

## Summary

DDS is fundamentally different from broker-based messaging because it is **data-centric**
(the middleware understands and manages the data's structure, identity, and lifecycle,
not just opaque bytes) and **brokerless/peer-to-peer** (publishers and subscribers
discover and talk to each other directly). This gives it three-way decoupling — in
**space** (no need to know peer locations), **time** (late joiners can get historical
data via Durability), and **flow** (producers and consumers are not rate-coupled) —
while offering fine-grained, per-topic **Quality of Service** control that lets a single
application mix hard-real-time flows with best-effort ones. This makes DDS the middleware
of choice for real-time, safety-critical, and highly dynamic distributed systems, whereas
Kafka/MQTT/AMQP remain better suited to event streaming, IoT telemetry at scale, and
enterprise integration where a central broker's operational simplicity and durable replay
semantics outweigh the need for brokerless, microsecond-latency delivery.

---

## Things to Remember

- **DDS is a specification, not a product** — many interoperable vendor implementations exist.
- **Data-centric**: DDS understands the data's type, key, and instance lifecycle — not just bytes.
- **Brokerless / peer-to-peer**: no intermediary sits in the data path → no single point of failure, lower latency.
- **Three-way decoupling**: space (auto-discovery), time (durability for late joiners), flow (independent producer/consumer rates).
- **QoS is per-topic/per-entity**, not global — a single app can mix reliable+durable and best-effort+volatile flows.
- **Interoperability guaranteed by RTPS**, the companion OMG wire-protocol standard.
- **Best fit**: real-time, safety-critical, low-latency, highly dynamic systems (robotics, avionics, industrial control).
- **Not the best fit** for durable event-log replay at massive scale (that's Kafka's specialty) or huge fleets of constrained/intermittent devices (that's MQTT's specialty).
