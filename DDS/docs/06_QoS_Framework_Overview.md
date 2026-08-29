# 06. QoS Framework Overview

## Overview

**Quality of Service (QoS)** is DDS's signature feature and arguably the single biggest
reason organizations choose it over broker-based alternatives. Instead of a middleware
that behaves the same way for every message, DDS lets every entity — Topic, Publisher,
Subscriber, DataWriter, DataReader, DomainParticipant — declare **contracts** about how
it wants data to be delivered: reliably or best-effort, with history or without,
bounded in time, exclusive or shared, and more. Over **20 standard QoS policies** exist,
and understanding the framework they fit into is a prerequisite for using any of them
correctly.

---

## QoS Is a Structured Set of Policies, Not a Single Knob

Each QoS policy is an independent, named structure with its own fields. An entity's
overall QoS is the combination of all applicable policies:

```cpp
DataWriterQos qos;
publisher->get_default_datawriter_qos(qos);

qos.reliability.kind = RELIABLE_RELIABILITY_QOS;
qos.durability.kind = TRANSIENT_LOCAL_DURABILITY_QOS;
qos.history.kind = KEEP_LAST_HISTORY_QOS;
qos.history.depth = 10;
qos.deadline.period = Duration_t{1, 0}; // 1 second

DataWriter* writer = publisher->create_datawriter(topic, qos);
```

The full policy list (each covered in its own topic in this series) includes:
**Reliability** (07), **Durability** (08), **History** and **Resource Limits** (09),
**Deadline** (10), **Liveliness** (11), **Ownership** and **Ownership Strength** (12),
**Partition** (13), **Lifespan** and **Time-Based Filter** (14), plus others such as
`Presentation`, `Destination Order`, `Latency Budget`, `Transport Priority`, and
`Writer/Reader Data Lifecycle`.

---

## Where QoS Applies: The Entity Hierarchy

Not every policy applies to every entity. QoS attaches at different levels, and lower
levels inherit sensible defaults from higher levels unless explicitly overridden:

```
DomainParticipant   → PARTICIPANT_QOS (transport, discovery, defaults for children)
 └── Topic          → TOPIC_QOS (defaults inherited by writers/readers of this topic)
 └── Publisher      → PUBLISHER_QOS (Partition, Presentation, plus default DataWriter QoS)
      └── DataWriter → DATAWRITER_QOS (Reliability, Durability, History, Deadline, ...)
 └── Subscriber     → SUBSCRIBER_QOS (Partition, Presentation, plus default DataReader QoS)
      └── DataReader → DATAREADER_QOS (Reliability, Durability, History, Deadline, ...)
```

A common, recommended pattern is to set sensible defaults at the **Topic** level (via
`TOPIC_QOS_DEFAULT` derived from the Topic's QoS) so that every DataWriter/DataReader
created for that topic inherits consistent behavior without repeating configuration
everywhere.

---

## Mutable vs. Immutable Policies

QoS policies are classified as either:

- **Mutable** — can be changed after entity creation via `set_qos()` (e.g., `Partition`,
  `Latency Budget`, `Ownership Strength`, `Time-Based Filter`).
- **Immutable** — fixed at entity creation time; changing them requires deleting and
  recreating the entity (e.g., `Reliability`, `History`, `Resource Limits`, `Durability`
  in most implementations).

Calling `set_qos()` with a change to an immutable policy raises an
`IMMUTABLE_POLICY` error. This distinction matters for dynamic reconfiguration design —
if your system needs to change reliability behavior at runtime, you must plan to
recreate the DataWriter/DataReader, not just call `set_qos()`.

---

## Requested vs. Offered: The Compatibility Model

The most conceptually important idea in the QoS framework is the **request/offered**
compatibility model, which governs whether a DataWriter and DataReader are allowed to
match (topic 15 covers this in depth):

- A **DataWriter offers** a certain level of service (e.g., `RELIABLE`, `depth=10`).
- A **DataReader requests** a certain level of service (e.g., `RELIABLE`, or
  `BEST_EFFORT`).
- They are **compatible** only if what the writer offers is "at least as good as" what
  the reader requests, per policy-specific compatibility rules.

```
Writer offers RELIABLE      + Reader requests BEST_EFFORT  → compatible (writer exceeds requirement)
Writer offers BEST_EFFORT   + Reader requests RELIABLE      → INCOMPATIBLE (writer can't satisfy)
Writer offers RELIABLE      + Reader requests RELIABLE      → compatible
```

Not all policies follow this asymmetric "offered ≥ requested" model — some (like
`Partition`) instead use a **matching/overlap** model rather than an ordering. Each
policy's own topic documents its specific compatibility semantics.

When policies are incompatible, the entities **do not match**, and both sides receive an
`on_offered_incompatible_qos` / `on_requested_incompatible_qos` status event —
critical for diagnosing "why isn't my reader getting data?" issues, since incompatible
QoS fails *silently* from a data-flow perspective (no error is thrown; the entities just
never connect).

---

## Setting QoS: Code, XML, and Profiles

QoS can be specified in application code (as shown above) or — in most production
systems — via **external XML QoS profile files**, which is strongly preferred for
maintainability:

```xml
<dds>
  <qos_library name="FleetQosLibrary">
    <qos_profile name="ReliableTelemetry">
      <datawriter_qos>
        <reliability><kind>RELIABLE_RELIABILITY_QOS</kind></reliability>
        <durability><kind>TRANSIENT_LOCAL_DURABILITY_QOS</kind></durability>
        <history><kind>KEEP_LAST_HISTORY_QOS</kind><depth>10</depth></history>
      </datawriter_qos>
    </qos_profile>
  </qos_library>
</dds>
```

```cpp
DataWriterQos qos;
factory->get_qos_from_profile(qos, "FleetQosLibrary::ReliableTelemetry");
DataWriter* writer = publisher->create_datawriter(topic, qos);
```

Benefits of XML-based QoS:

- Decouples **operational tuning** from application code/rebuilds.
- Enables per-deployment overrides (dev vs. prod tuning) without touching source.
- Centralizes QoS policy so different teams/modules stay consistent.

---

## A Practical Mental Model

Think of QoS not as "tuning knobs" but as **a contract negotiation language between
independently-developed publishers and subscribers**. In a broker-based system, the
broker's behavior is largely uniform and configured centrally/administratively. In DDS,
*every data flow* can independently declare its own contract — which is what makes DDS
suitable for systems mixing hard-real-time control loops (deadline + reliable +
ownership-exclusive) with best-effort telemetry (best-effort + volatile) in the same
application, over the same domain, without one flow's requirements compromising another's.

---

## Summary

The QoS framework is a structured set of 20+ independent, named policies that attach at
different levels of the DDS entity hierarchy (DomainParticipant, Topic, Publisher/
Subscriber, DataWriter/DataReader), with lower levels inheriting configurable defaults
from higher ones. Policies are either mutable (changeable via `set_qos()` at runtime) or
immutable (fixed at creation), and — critically — a DataWriter and DataReader only match
and exchange data when their policies are **compatible** under DDS's requested-vs-offered
model, with incompatibilities surfaced via status events rather than exceptions. QoS
is best managed via external XML profiles in production systems, decoupling operational
tuning from application code. The framework's real power is letting each data flow in a
system declare its own delivery contract independently — enabling hard-real-time and
best-effort traffic to coexist cleanly in the same application.

---

## Things to Remember

- **QoS is a set of independent, named policies**, not a single setting — over 20 exist, each with its own semantics.
- **Policies attach at different entity levels** (Participant, Topic, Publisher/Subscriber, DataWriter/DataReader) with inheritance of defaults from higher levels.
- **Mutable policies** can be changed via `set_qos()` at runtime; **immutable policies** require deleting and recreating the entity.
- **The requested-vs-offered compatibility model** determines whether a writer/reader pair matches — most (not all) policies require "offered ≥ requested."
- **Incompatible QoS fails silently from a data-flow perspective** — watch `on_offered_incompatible_qos`/`on_requested_incompatible_qos` to diagnose non-matching entities.
- **Prefer external XML QoS profiles** over hardcoded QoS in production systems, for maintainability and deployment-specific tuning.
- **QoS lets independent data flows in the same system have wildly different delivery contracts** — this is DDS's core differentiator vs. uniform broker-based delivery.
