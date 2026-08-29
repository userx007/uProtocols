# 02. Domain and DomainParticipant

## Overview

Before any publisher or subscriber can exchange data in DDS, it must join a **Domain** —
the top-level isolation boundary of a DDS deployment — through a **DomainParticipant**,
the entry-point entity of the DDS entity model. Understanding these two concepts
correctly is foundational: nearly every bug related to "why can't my nodes see each
other?" traces back to a domain or participant misconfiguration.

---

## The Domain

A **Domain** is a logical network of DDS applications that can discover and communicate
with one another. It is identified by a **Domain ID** — a small non-negative integer
(commonly 0–232, though the practical/portable range is vendor- and deployment-dependent).

Key properties:

- **Isolation, not physical separation.** Two applications on the same physical network
  (same subnet, same multicast group) but configured with *different* Domain IDs will
  **never** discover each other — DDS enforces isolation logically, at the protocol
  level, not just via network topology.
- **The Domain ID maps to network ports.** In the standard RTPS discovery scheme
  (topic 20), the Domain ID is used to compute the UDP ports used for multicast
  discovery traffic, via a formula roughly of the form:

  ```
  PB = 7400                     # Port Base
  DG = 250                      # Domain Gain
  spdp_multicast_port = PB + DG * domain_id
  ```

  This is *why* changing the Domain ID is often the fastest way to run multiple,
  fully-isolated DDS test environments on the same LAN without them interfering with
  each other.
- **One domain = one "universe" of Topics.** A Topic named `"VehiclePosition"` in Domain 0
  is a completely distinct entity from a Topic of the same name in Domain 1 — no data
  ever crosses domain boundaries unless explicitly bridged (e.g., via a Routing Service,
  topic 30).
- **Practical use of multiple domains:**
  - Separate **environments**: dev = domain 0, staging = domain 1, prod = domain 2.
  - Separate **subsystems** that must never accidentally cross-talk (e.g., a simulation
    domain vs. a live-vehicle domain).
  - **Multi-tenancy** on shared infrastructure.

### Example: two isolated domains on the same host

```cpp
// Application A — Domain 0
DomainParticipant* participantA = factory->create_participant(0 /* domain_id */, PARTICIPANT_QOS_DEFAULT);

// Application B — Domain 1, same machine, same topic name/type
DomainParticipant* participantB = factory->create_participant(1 /* domain_id */, PARTICIPANT_QOS_DEFAULT);
```

Even though A and B run on the same host and could define an identical
`"VehiclePosition"` topic, **they will never discover each other** — they are on
different domains. This is a deliberate, commonly-used isolation mechanism, not a bug.

---

## The DomainParticipant

A **DomainParticipant** is the DDS entity that represents an application's membership in
a Domain. It is the **factory for every other entity**:

```
DomainParticipant
 ├── creates → Topic
 ├── creates → Publisher     → creates → DataWriter
 └── creates → Subscriber    → creates → DataReader
```

Responsibilities of the DomainParticipant:

- **Joining the domain** — starting participant discovery (SPDP, topic 20) so it becomes
  visible to (and can discover) other participants on the same Domain ID.
- **Resource ownership** — it owns and manages the lifecycle of every Topic, Publisher,
  Subscriber, DataWriter, and DataReader created under it. Deleting a participant
  cascades and deletes all of its child entities.
- **Default QoS scope** — it holds default QoS profiles that child entities inherit
  unless overridden.
- **Domain-wide configuration** — network interfaces, discovery peers, transport
  settings, and (if used) security settings are typically configured at the participant
  level via QoS or vendor-specific XML configuration.

### Typical lifecycle

```cpp
// 1. Get the singleton factory
DomainParticipantFactory* factory = DomainParticipantFactory::get_instance();

// 2. Create a participant on a given domain
DomainParticipant* participant = factory->create_participant(
    42,                          // Domain ID
    PARTICIPANT_QOS_DEFAULT,     // QoS
    nullptr,                     // listener
    STATUS_MASK_NONE);

// 3. Use the participant as a factory for Topics, Publishers, Subscribers...
Topic* topic = participant->create_topic("VehiclePosition", "VehiclePosition::Type",
                                          TOPIC_QOS_DEFAULT);
Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);

// ... application runs, entities exchange data ...

// 4. Clean shutdown: delete contained entities, then the participant, then finalize factory
participant->delete_contained_entities();
factory->delete_participant(participant);
factory->finalize_instance();
```

### One participant vs. many per process

A common design question: should a process create **one DomainParticipant** (shared by
all its topics) or **several** (e.g., one per subsystem)?

| Approach | Pros | Cons |
|---|---|---|
| **Single participant per process** | Lower discovery overhead (one set of discovery announcements); simpler resource management; recommended default | All entities share one identity/lifecycle; a participant-level QoS change affects everything |
| **Multiple participants per process** | Logical isolation within a process (e.g., different partitions, different security identities, different transport configs); can join different domains from one process | More discovery traffic (each participant announces itself separately, O(n²) scaling concerns — see topic 22); more resource overhead |

**Rule of thumb:** default to **one participant per process per domain**, and only
create additional participants when you have a concrete reason (different domain,
different security identity, or isolating discovery/QoS scope).

---

## How Domain and DomainParticipant Interact With Discovery

When a DomainParticipant is created:

1. It begins sending/listening for **SPDP** (Simple Participant Discovery Protocol)
   announcements on the multicast (and/or unicast) address/port derived from its Domain
   ID (topic 20).
2. When it discovers another participant announcing the **same Domain ID**, the two
   participants exchange metadata and proceed to **SEDP** (Simple Endpoint Discovery
   Protocol, topic 21) to match compatible DataWriters/DataReaders.
3. Participants on *different* Domain IDs simply never engage in this handshake — from
   each other's perspective, they don't exist.

This is why **Domain ID mismatches** (e.g., one node misconfigured with domain 0 instead
of domain 5) are one of the most common "why won't my publisher and subscriber connect?"
troubleshooting scenarios in real deployments.

---

## Summary

The **Domain** is DDS's top-level, logical isolation boundary — identified by a small
integer Domain ID that (via a well-known formula) also determines the discovery network
ports, meaning two applications on different Domain IDs are completely invisible to each
other even on the same physical network. The **DomainParticipant** is the entry-point
entity an application uses to join a Domain and acts as the factory for every other DDS
entity (Topics, Publishers, Subscribers, DataWriters, DataReaders), owning their
lifecycle and providing default QoS scope. Getting the Domain ID right — and choosing a
sensible participant-per-process strategy — is the first and most common troubleshooting
checkpoint in any DDS deployment.

---

## Things to Remember

- **Domain ID isolates communication logically**, not just physically — different domain IDs never see each other, even on the same LAN/host.
- **Domain ID determines discovery ports** via a standard formula (`PB + DG × domain_id` in default RTPS discovery) — useful for running isolated test environments side by side.
- **DomainParticipant is the factory** for Topics, Publishers, Subscribers, DataWriters, and DataReaders — deleting it cascades to all children.
- **Default to one participant per process per domain** unless you have a specific reason (multiple domains, isolated QoS/security scope) for more — extra participants increase discovery overhead.
- **Domain ID mismatch is the #1 "nodes can't see each other" bug** — always check this first when troubleshooting connectivity.
- **Domains ≠ Partitions** — Partitions (topic 13) are a *finer-grained*, in-domain filtering mechanism; Domains are a *hard*, protocol-level isolation boundary.
