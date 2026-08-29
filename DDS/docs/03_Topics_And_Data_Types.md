# 03. Topics and Data Types

## Overview

The **Topic** is the central organizing concept of DDS's data-centric model. It is the
"noun" that everything else revolves around: DataWriters write to a Topic, DataReaders
read from a Topic, and DDS's discovery and matching logic is fundamentally about finding
compatible writers and readers **of the same Topic**.

A Topic binds together three things:

1. A **unique name** (a string, e.g. `"VehiclePosition"`) — scoped to the Domain (topic 02).
2. A **data type** — a strongly-typed structure, normally defined in IDL (topic 23).
3. A set of **QoS policies** that apply as defaults/constraints for entities using it.

```
Topic = Name + Type + QoS
```

Two applications only interoperate on a given Topic if they agree on **all three** —
mismatched names, incompatible types, or incompatible QoS all prevent matching.

---

## Why the Topic Matters: It's Not Just a Channel Name

In broker-based systems, a "topic" or "queue name" is usually just a routing key — an
opaque string the broker uses to fan messages out. In DDS, the Topic is a **first-class,
typed entity** that the middleware itself understands and enforces:

- **Type safety is structural, not just nominal.** DDS implementations perform (or can
  perform, depending on XTypes configuration — topic 26) type compatibility checks at
  discovery time. A writer and reader with the same topic name but incompatible types
  simply won't match — you get a protected failure mode, not silent data corruption.
- **The Topic carries semantic meaning for keyed instances.** Because the data type can
  declare `@key` fields (topic 24), the Topic isn't just "a stream of messages" — it's a
  managed space of **instances**, each with independent lifecycle, history, and QoS
  application.
- **QoS is topic-scoped.** You can — and often should — give different topics wildly
  different QoS: a high-frequency sensor topic might be `BEST_EFFORT` + `VOLATILE`,
  while a configuration topic is `RELIABLE` + `TRANSIENT_LOCAL`.

---

## Defining a Topic's Data Type (IDL)

Data types are conventionally defined in **IDL (Interface Definition Language)** and
compiled by a vendor-specific code generator (`rtiddsgen`, `fastddsgen`, `idlc`, etc.)
into language bindings (C++, Java, Python, C, Rust...).

```idl
module Fleet {
    struct VehiclePosition {
        @key long vehicle_id;
        double latitude;
        double longitude;
        double speed_kmh;
        long long timestamp_ms;
    };
};
```

This generates, e.g. in C++, a `Fleet::VehiclePosition` class with accessors, plus the
type-support code DDS needs to (de)serialize it (CDR — topic 25) and register it with a
participant.

### Registering and creating a Topic

```cpp
// 1. Register the type with the participant (ties the IDL-generated type to a name DDS uses internally)
Fleet::VehiclePositionTypeSupport::register_type(participant, "Fleet::VehiclePosition");

// 2. Create the Topic: binds a Topic *name* to the registered *type name*, plus QoS
Topic* topic = participant->create_topic(
    "VehiclePosition",              // Topic name (used for discovery/matching)
    "Fleet::VehiclePosition",       // Registered type name
    TOPIC_QOS_DEFAULT);
```

Note the two distinct strings: the **Topic name** (what applications match on) and the
**type name** (what defines the data's shape). It's entirely possible — and sometimes
useful — to have multiple Topics share the same type, or the same-named Topic evolve its
type over time via XTypes-compatible extensions.

---

## Kinds of Topics

DDS defines more than one topic-like entity, though the plain `Topic` is by far the most
common:

| Kind | Purpose |
|---|---|
| **Topic** | The standard case: a named, typed data stream. |
| **ContentFilteredTopic** | A "view" over a Topic that only delivers samples matching a filter expression (SQL-like syntax), evaluated **before** transmission where supported — see topic 35. |
| **MultiTopic** | Combines/joins data from multiple related Topics into a single derived stream (less commonly used; support varies by vendor). |

### Example: ContentFilteredTopic

```cpp
ContentFilteredTopic* fastVehicles = participant->create_contentfilteredtopic(
    "FastVehicles",
    topic,                             // base Topic
    "speed_kmh > %0",                  // filter expression
    {"80"});                           // parameter

DataReader* reader = subscriber->create_datareader(fastVehicles, DATAREADER_QOS_DEFAULT);
// This reader only receives samples where speed_kmh > 80 — filtering can happen
// writer-side (if supported) to save bandwidth, not just reader-side.
```

---

## Built-in Topics

DDS implementations expose a set of **built-in Topics** that describe the domain's own
state — participants, publications, and subscriptions currently discovered. These are
ordinary Topics (readable via ordinary DataReaders) that provide introspection:

- `DCPSParticipant` — discovered DomainParticipants
- `DCPSPublication` — discovered DataWriters and their QoS
- `DCPSSubscription` — discovered DataReaders and their QoS
- `DCPSTopic` — discovered Topics (in some implementations)

These are invaluable for building monitoring/diagnostic tooling (topic 36) — e.g., a
dashboard that lists every writer/reader currently active in the domain, without any
custom instrumentation.

```cpp
DataReader* builtinPubReader = participant->get_builtin_subscriber()
    ->lookup_datareader("DCPSPublication");
// Read this to see every DataWriter currently discovered in the domain, with its QoS.
```

---

## Topic Naming and Design Practices

- **Use clear, hierarchical, stable names.** Since renaming a Topic is a breaking change
  for every participant, treat Topic names like a public API contract
  (e.g., `"Fleet::VehiclePosition"` or `"/fleet/vehicle_position"` depending on vendor
  convention — ROS 2 uses a path-like convention under the hood, topic 37).
- **One Topic = one semantic concept**, not a dumping ground for unrelated fields. Split
  data with very different QoS needs (e.g., high-rate telemetry vs. rare config) into
  separate Topics even if conceptually related.
- **Design the key fields deliberately** (topic 24) — the key defines what "an instance"
  means, and that decision is hard to change later without breaking compatibility.
- **Plan for evolution up front.** Decide whether the type is `final`, `appendable`, or
  `mutable` under XTypes (topic 26) — this determines whether you can add optional
  fields later without breaking older readers/writers.

---

## Summary

The Topic is DDS's core organizing abstraction: a named, strongly-typed, QoS-scoped
channel that DataWriters and DataReaders must agree on (name, type, and compatible QoS)
in order to match and exchange data. Unlike a broker's opaque routing key, a DDS Topic is
understood by the middleware itself, enabling type-safe matching, per-instance lifecycle
management via key fields, content-based filtering (ContentFilteredTopic), and rich
introspection via built-in Topics. Careful, deliberate Topic and type design — names,
key fields, and extensibility strategy — pays off enormously over an application's
lifetime, since Topics function as a durable public contract between independently
evolving publishers and subscribers.

---

## Things to Remember

- **Topic = Name + Type + QoS** — all three must be compatible for a writer and reader to match.
- **Data types are normally defined in IDL** and compiled into language bindings by a vendor code generator.
- **Topic name ≠ type name** — a Topic name is the matching key; the type name defines the data shape; they're registered separately.
- **ContentFilteredTopic** lets readers subscribe to a filtered subset of a Topic, sometimes with writer-side filtering to save bandwidth.
- **Built-in Topics** (`DCPSParticipant`, `DCPSPublication`, `DCPSSubscription`) give free, standardized introspection into the domain's live state.
- **Treat Topic names and key-field design as a public API contract** — changing them later breaks compatibility across independently deployed applications.
- **Split by QoS needs, not just by "relatedness"** — don't force high-rate and low-rate data into the same Topic.
