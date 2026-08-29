# 14. Lifespan and Time-Based Filter QoS

## Overview

**Lifespan** and **Time-Based Filter** are two smaller, but practically valuable, QoS
policies that both deal with **time-bounding data relevance** — but from opposite ends
of the pipeline. Lifespan (writer side) says *"this data expires — stop delivering it
after a certain age."* Time-Based Filter (reader side) says *"I don't need every
update — throttle what you deliver to me."* Together they let engineers control exactly
how much stale or excessive data flows through the system, independent of Reliability
or History.

---

## Lifespan QoS

```cpp
qos.lifespan.duration = Duration_t{5, 0}; // samples expire 5 seconds after being written
```

Lifespan sets an **expiration time** on every sample a DataWriter publishes. Once a
sample's age exceeds `lifespan.duration` (measured from its source timestamp), DDS
**automatically removes it** from:

- The writer's history queue (it will no longer be retransmitted, even under `RELIABLE`
  reliability, once expired).
- The reader's cache (an expired sample that hasn't been `take()`n yet is dropped rather
  than delivered to the application).
- **Durability replay** — a late-joining reader under `TRANSIENT_LOCAL`/`TRANSIENT`/
  `PERSISTENT` durability will **not** receive samples that have already expired, even
  though they'd otherwise be eligible for replay.

### Why this matters: preventing stale data delivery

Without Lifespan, a `RELIABLE` + `KEEP_ALL` writer talking to a reader that's been
disconnected for, say, 30 seconds will faithfully retransmit **every** sample from those
30 seconds once the reader reconnects — potentially including data that's now
meaningless (e.g., a sensor reading, a stale command, an outdated alert). Lifespan
guarantees that no matter how "reliable" or "durable" the delivery mechanism, data older
than its declared relevance window is simply never delivered.

```cpp
// A "current alarm" topic: an alarm older than 10 seconds is meaningless — don't deliver it late
DataWriterQos alarmQos;
alarmQos.reliability.kind = RELIABLE_RELIABILITY_QOS;
alarmQos.durability.kind = TRANSIENT_LOCAL_DURABILITY_QOS;
alarmQos.lifespan.duration = Duration_t{10, 0};
// A reader reconnecting after a 30-second outage will NOT receive alarms from that gap
// once they're older than 10 seconds — preventing a flood of stale alerts on reconnect.
```

### Lifespan vs. History — different failure modes addressed

| Policy | Bounds... | By... |
|---|---|---|
| **History** | *How many* samples are retained per instance | Count (`depth`) |
| **Lifespan** | *How old* a sample can be before it's discarded | Time (`duration`) |

They're complementary and often used together: History bounds memory; Lifespan bounds
staleness — a fast-writing, slow-consuming scenario might exceed History's depth limit
long before Lifespan's duration limit, or vice versa, depending on the data's actual
publish rate.

---

## Time-Based Filter QoS

```cpp
qos.time_based_filter.minimum_separation = Duration_t{0, 200 * 1000000}; // 200ms
```

Time-Based Filter is set on the **DataReader** and instructs DDS to deliver **at most
one sample per instance per `minimum_separation` interval**, even if the writer is
publishing much faster. It is, in effect, a reader-side **throttle** or **decimation**
filter — the inverse problem from Lifespan (which discards *old* data; Time-Based
Filter discards *excess/too-frequent* data).

### Why this matters: protecting slow consumers without slowing down fast producers

A classic scenario: a DataWriter publishes vehicle position updates at 100 Hz (every
10ms) because that's what the primary consumer (a real-time control loop) needs. But a
secondary consumer — say, a dashboard UI updating at 5 Hz — doesn't need (and would be
wasted effort processing) 100 samples per second; it only needs one every 200ms.

```cpp
// High-rate writer: publishes at 100Hz regardless of any reader's needs
writer->write(position); // called every 10ms

// Dashboard reader: only wants an update every 200ms, even though writer publishes every 10ms
DataReaderQos dashboardQos;
dashboardQos.time_based_filter.minimum_separation = Duration_t{0, 200 * 1000000};
DataReader* dashboardReader = subscriber->create_datareader(topic, dashboardQos);

// Control-loop reader: no filter, wants every sample
DataReaderQos controlQos; // time_based_filter left at default (0 = no filtering)
DataReader* controlReader = subscriber->create_datareader(topic, controlQos);
```

Crucially, this filtering happens **without requiring the writer to publish at multiple
different rates** — the *same* DataWriter, publishing at its natural rate, can serve
both a high-rate control-loop reader and a low-rate dashboard reader simultaneously,
each getting exactly the cadence it needs. Depending on the implementation, this
filtering can even be applied **at the writer side** (reducing actual network traffic to
that specific reader), not just discarded after arrival at the reader — a meaningful
bandwidth optimization on constrained networks.

### Time-Based Filter is per-instance

Like History, Deadline, and Ownership, Time-Based Filter operates per keyed instance —
a reader tracking many instances gets independent throttling for each one, not one
global rate limit across the whole topic.

---

## Using Both Together

```cpp
// Fleet dashboard: doesn't need every sample, and definitely doesn't want stale ones
// after a brief network hiccup
DataReaderQos dashboardQos;
dashboardQos.time_based_filter.minimum_separation = Duration_t{1, 0}; // at most 1 update/sec

DataWriterQos writerQos;
writerQos.lifespan.duration = Duration_t{5, 0}; // don't deliver anything older than 5s
```

This combination is common for **UI/monitoring consumers** of high-rate telemetry
Topics that are also consumed by real-time control loops: the control loop gets
Lifespan protection against acting on stale data, while the dashboard gets both
Lifespan protection *and* Time-Based Filter throttling, without requiring any change to
how the writer itself publishes.

---

## Practical Guidance

- **Use Lifespan on any Topic where "late" data is actively harmful or meaningless** —
  alarms, commands, time-sensitive sensor readings — especially when combined with
  `RELIABLE` + `TRANSIENT_LOCAL`/`TRANSIENT`, where without Lifespan a reconnecting
  reader could otherwise receive a burst of outdated data.
- **Use Time-Based Filter to let one DataWriter efficiently serve consumers with very
  different rate needs**, instead of either over-serving slow consumers or creating
  multiple differently-rated Topics for the same underlying data.
- **Remember both are per-instance** — design and reason about them at the instance
  level, especially in topics tracking many independent keyed entities.
- **Time-Based Filter doesn't reduce what the writer computes/sends by default in every
  implementation** — check whether your vendor applies the filter at the writer
  (saving bandwidth) or purely at the reader (saving application-level processing but
  not network bandwidth); this affects whether it's useful for bandwidth-constrained
  links specifically.

---

## Summary

Lifespan and Time-Based Filter both bound the *temporal relevance* of DDS data, from
opposite ends of the pipeline. Lifespan (writer-side) automatically expires and discards
samples older than a configured duration — from the writer's queue, the reader's cache,
and durability replay — preventing stale data from ever reaching an application no
matter how reliable or durable the delivery path is. Time-Based Filter (reader-side)
throttles delivery to at most one sample per instance per configured interval, letting a
single fast-publishing DataWriter efficiently serve both high-rate (e.g., control-loop)
and low-rate (e.g., dashboard) consumers without publishing at multiple rates or
creating duplicate Topics. Both operate per keyed instance and are commonly used
together for Topics consumed by both real-time and human-facing/monitoring subscribers.

---

## Things to Remember

- **Lifespan (writer-side)**: expires samples older than `duration` — removed from writer queue, reader cache, and durability replay alike.
- **Time-Based Filter (reader-side)**: delivers at most one sample per instance per `minimum_separation` — a per-reader throttle, not a writer-side rate change.
- **Both operate per keyed instance**, not as one global limit across a topic.
- **Lifespan prevents "stale burst on reconnect"** — critical for `RELIABLE` + durable topics where late delivery of old data would be actively harmful.
- **Time-Based Filter lets one writer efficiently serve consumers needing very different update rates**, without multiple Topics or multiple publish rates.
- **Check whether your vendor applies Time-Based Filter at the writer** (saves network bandwidth) **or only at the reader** (saves application processing only) — this affects its usefulness on constrained links.
- **Use together** for Topics with both real-time and monitoring/UI-style consumers.
