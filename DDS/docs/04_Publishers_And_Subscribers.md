# 04. Publishers and Subscribers

## Overview

Between the **DomainParticipant** (topic 02) and the actual data-moving entities —
**DataWriters** and **DataReaders** (topic 05) — DDS interposes two container entities:
**Publisher** and **Subscriber**. They are frequently under-appreciated, because a huge
number of applications never need more than the default one-of-each, but understanding
what they actually do (and don't do) matters for anyone tuning performance, coordinating
multi-writer consistency, or organizing large applications.

```
DomainParticipant
 ├── Publisher   → creates/owns → DataWriter(s)
 └── Subscriber  → creates/owns → DataReader(s)
```

A Publisher **owns and manages one or more DataWriters**; a Subscriber **owns and manages
one or more DataReaders**. Neither is tied to a single Topic — a single Publisher can
have DataWriters for many different Topics, and likewise for a Subscriber.

---

## What a Publisher Actually Does

The Publisher is not just a "bag of DataWriters" — it has real responsibilities:

1. **Factory and lifecycle owner** for its DataWriters — deleting a Publisher deletes all
   DataWriters it created.
2. **Default QoS scope** — DataWriters inherit `DATAWRITER_QOS_DEFAULT` values that
   ultimately trace back through the Publisher's default QoS.
3. **Coordinated (grouped) sample publication**, via `suspend_publications()` /
   `resume_publications()`. This lets you group writes from multiple DataWriters under
   the same Publisher so that they are released to the transport **together**, which is
   important when a subscriber needs to see a consistent, atomic snapshot across several
   related Topics (e.g., updating `Position` and `Velocity` for the same object
   "at once").
4. **PARTITION QoS application point** — Partition (topic 13) is set at the
   Publisher/Subscriber level, not per-DataWriter/DataReader, meaning every DataWriter
   under a Publisher shares the same partition membership by default.
5. **Presentation QoS** — controls whether changes across multiple instances/topics under
   this Publisher are delivered to matching Subscribers in a coordinated (grouped) or
   independent (instance-by-instance) way. `PRESENTATION` QoS has an `access_scope`
   (`INSTANCE`, `TOPIC`, or `GROUP`) that determines the granularity of this coordination.

### Example: coordinated multi-writer update

```cpp
Publisher* pub = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
DataWriter* positionWriter = pub->create_datawriter(positionTopic, DATAWRITER_QOS_DEFAULT);
DataWriter* velocityWriter = pub->create_datawriter(velocityTopic, DATAWRITER_QOS_DEFAULT);

pub->suspend_publications();
positionWriter->write(newPosition);
velocityWriter->write(newVelocity);
pub->resume_publications();
// With PRESENTATION access_scope = GROUP and coherent_access = true,
// subscribers see position and velocity updates as one atomic, coherent change set.
```

---

## What a Subscriber Actually Does

Symmetrically, the Subscriber:

1. **Factory and lifecycle owner** for its DataReaders.
2. **Default QoS scope** for DataReaders it creates.
3. **PARTITION QoS application point** — shared partition membership across all
   DataReaders it owns.
4. **Coordinated access to multiple DataReaders' data**, via
   `begin_access()` / `end_access()`, paired with `PRESENTATION` QoS on the
   Subscriber side — enabling an application to read a **consistent set of changes**
   across multiple related DataReaders (mirroring the Publisher's coherent grouping).
5. **Aggregated status/data-available notification** — a Subscriber-level listener can
   be notified when *any* of its DataReaders has new data, useful for building generic
   dispatch loops without registering a listener per DataReader.

### Example: coherent read across two DataReaders

```cpp
Subscriber* sub = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
DataReader* positionReader = sub->create_datareader(positionTopic, DATAREADER_QOS_DEFAULT);
DataReader* velocityReader = sub->create_datareader(velocityTopic, DATAREADER_QOS_DEFAULT);

sub->begin_access();
positionReader->take(positionSamples, positionInfos);
velocityReader->take(velocitySamples, velocityInfos);
sub->end_access();
// With matching PRESENTATION QoS, the samples taken here reflect one coherent
// publisher-side update, not a torn read across two independent writes.
```

---

## Why Not Just Use One Publisher/Subscriber for Everything?

Most applications *do* just use one default Publisher and one default Subscriber per
participant — that's a perfectly reasonable default. You reach for multiple
Publishers/Subscribers within a participant when you need:

| Reason | Explanation |
|---|---|
| **Different Partition sets** | e.g., one Publisher writes to partition `"SimA"`, another to `"SimB"`, from the same process. |
| **Different default QoS profiles** | e.g., one Publisher default-configured for reliable/durable writers, another for best-effort/volatile ones, to avoid repeating QoS on every DataWriter. |
| **Independent coherent groups** | Two logically unrelated sets of writers that each need their own atomic-update grouping, without interfering with each other. |
| **Organizational clarity** | Large systems sometimes split Publishers/Subscribers by subsystem for readability and independent lifecycle management (e.g., shutting down one subsystem's writers without touching another's). |

---

## Publisher/Subscriber vs. DataWriter/DataReader — Don't Confuse the Layers

A common point of confusion for engineers new to DDS: **Partition** and **Presentation**
QoS live on the Publisher/Subscriber, while **Reliability**, **Durability**, **History**,
**Deadline**, **Ownership**, **Lifespan**, etc. live on the DataWriter/DataReader (or
Topic, as defaults). If two DataWriters under the same Publisher need genuinely different
delivery guarantees (one reliable, one best-effort), that's fine — those policies are set
per-DataWriter, not inherited rigidly from the Publisher, aside from default QoS
inheritance which can always be overridden per entity.

```
                 Sets:                          Sets:
Publisher   →   Partition, Presentation   |  DataWriter → Reliability, Durability,
Subscriber  →   Partition, Presentation   |  DataReader →  History, Deadline, Ownership,
                                           |               Lifespan, Liveliness, etc.
```

---

## Summary

Publisher and Subscriber are the container/factory entities that sit between the
DomainParticipant and the actual DataWriters/DataReaders that move data. Beyond simple
lifecycle ownership, they are the application point for **Partition** QoS (shared
partition membership across everything they own) and **Presentation** QoS (coordinated,
atomic delivery of related changes across multiple writers or readers via
suspend/resume and begin/end access). Most applications need only one default Publisher
and Subscriber per participant; reach for additional ones when you need distinct
partition sets, distinct default QoS profiles, or independent coherent-update groups.

---

## Things to Remember

- **Publisher owns DataWriters; Subscriber owns DataReaders** — deleting the container cascades to its children.
- **Partition and Presentation QoS are set on Publisher/Subscriber**, not on individual DataWriters/DataReaders.
- **`suspend_publications()`/`resume_publications()`** (Publisher) and **`begin_access()`/`end_access()`** (Subscriber), paired with `PRESENTATION` QoS, enable atomic/coherent multi-writer or multi-reader updates.
- **One default Publisher + one default Subscriber per participant is the common case** — add more only for distinct partitions, distinct default QoS, or independent coherent groups.
- **Reliability/Durability/History/Deadline/Ownership/etc. live at the DataWriter/DataReader (or Topic) level**, not the Publisher/Subscriber level — don't confuse the two layers of QoS.
