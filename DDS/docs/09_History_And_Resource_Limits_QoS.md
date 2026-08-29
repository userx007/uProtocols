# 09. History and Resource Limits QoS

## Overview

**History** and **Resource Limits** are two tightly-coupled QoS policies that answer a
question every middleware must eventually confront: **when a producer is faster than a
consumer, or a consumer is temporarily disconnected, what happens to the samples that
pile up?** History defines the *policy* (how many samples to keep, per instance).
Resource Limits defines the *hard caps* (how much memory the middleware is allowed to
consume enforcing that policy). Together, they determine DDS's memory footprint,
backpressure behavior, and interact directly with Reliability (topic 07) and Durability
(topic 08).

---

## History QoS

```cpp
qos.history.kind = KEEP_LAST_HISTORY_QOS;  // or KEEP_ALL_HISTORY_QOS
qos.history.depth = 10;                    // only meaningful for KEEP_LAST
```

History controls how many samples are retained **per instance** (topic 24) — not a flat
global count — in both the DataWriter's send/retransmit queue and the DataReader's
receive queue.

### `KEEP_LAST` (the default)

- Retains only the **most recent `depth` samples** per instance.
- Older samples are silently discarded once the depth limit is exceeded — for the
  writer, this means they can no longer be retransmitted; for the reader, it means
  they're dropped from the local cache once read/superseded.
- `depth = 1` is extremely common: "I only care about the latest value of each
  instance" — e.g., current position, current temperature, current status.
- Predictable, bounded memory usage: `depth × number_of_instances × sample_size`
  (roughly).

### `KEEP_ALL`

- Retains **every sample** for every instance, up to the bound imposed by
  `RESOURCE_LIMITS` (there is no unbounded buffering in DDS — `KEEP_ALL` is bounded by
  resource limits, not literally infinite).
- Needed when an application must process **every** sample, not just the latest (e.g.,
  an audit log, a sequence of discrete commands where none can be skipped, an event
  stream where intermediate values matter).
- Combined with `RELIABLE` reliability, `KEEP_ALL` is what guarantees "every sample the
  writer sent, the reader eventually receives, in order, with none dropped or skipped" —
  `KEEP_LAST` with a shallow depth can silently skip samples even under `RELIABLE`
  reliability if the reader falls behind.

### A critical distinction: `KEEP_LAST` + `RELIABLE` still allows skipped samples

A common misconception is that `RELIABLE` means "I will receive every sample." That's
only true if History is `KEEP_ALL` (or a `KEEP_LAST` depth large enough to never be
exceeded before the reader catches up). `RELIABLE` actually guarantees: *"every sample
that is still in the writer's history queue when a reader falls behind will be
delivered."* If `KEEP_LAST depth=1` and the writer publishes samples faster than a slow
reader can consume them, older samples are **evicted from the queue** before the reader
ever acknowledges them — reliably delivering the *latest* value, but not every value.

```
RELIABLE + KEEP_LAST(depth=1)  → guarantees the reader eventually gets the LATEST value, not every value
RELIABLE + KEEP_ALL            → guarantees the reader gets EVERY value, in order (bounded by resource limits)
```

---

## Resource Limits QoS

```cpp
qos.resource_limits.max_samples = 1000;
qos.resource_limits.max_instances = 100;
qos.resource_limits.max_samples_per_instance = 10;
```

Resource Limits impose **hard caps** on memory usage, independent of (but coordinated
with) History:

| Field | Meaning |
|---|---|
| `max_samples` | Total samples across all instances a writer/reader will buffer. |
| `max_instances` | Total distinct instances (keyed values) a writer/reader will track. |
| `max_samples_per_instance` | Cap per individual instance — must be ≥ `history.depth` when using `KEEP_LAST`, and effectively defines the ceiling for `KEEP_ALL`. |

These exist specifically to prevent **unbounded memory growth** — without them, a
`KEEP_ALL` writer talking to a permanently-disconnected or very slow reliable reader
could accumulate samples indefinitely. Resource Limits guarantee DDS never silently
consumes unbounded memory, at the cost of the writer potentially **blocking** on
`write()` (up to `max_blocking_time`, topic 07) or **rejecting** new instances once caps
are hit.

### `max_samples_per_instance` must be consistent with `History.depth`

Implementations require `max_samples_per_instance ≥ history.depth` (for `KEEP_LAST`) —
setting them inconsistently is a configuration error caught at entity-creation time in
most vendors.

---

## How These Interact With Reliability and Durability

| Policy combo | Practical effect |
|---|---|
| `BEST_EFFORT` + `KEEP_LAST depth=1` | Classic "latest value wins" sensor stream — minimal memory, no retransmission, older values are simply irrelevant. |
| `RELIABLE` + `KEEP_LAST depth=N` | Reader guaranteed to get the *latest N* samples per instance reliably — good for state topics where only recent history matters. |
| `RELIABLE` + `KEEP_ALL` | Reader guaranteed to get *every* sample, in order — required for command/event streams; writer `write()` can block if a reader falls far behind and resource limits are reached. |
| `TRANSIENT_LOCAL` + `KEEP_LAST depth=1` | Late joiner receives just the current value of every instance — the standard "current state/config" pattern (topic 08). |
| `TRANSIENT_LOCAL` + `KEEP_ALL` | Late joiner receives the full retained history of every instance — used when replay of a bounded event log matters, e.g., recent alarm history. |

---

## Example: choosing History/Resource Limits for two different topics

```cpp
// Topic 1: high-rate vehicle position — only the latest value per vehicle matters
DataWriterQos posQos;
posQos.history.kind = KEEP_LAST_HISTORY_QOS;
posQos.history.depth = 1;
posQos.resource_limits.max_samples_per_instance = 1;
posQos.resource_limits.max_instances = 500;       // up to 500 tracked vehicles
posQos.resource_limits.max_samples = 500;

// Topic 2: discrete mission commands — every command must be processed, none skipped
DataWriterQos cmdQos;
cmdQos.reliability.kind = RELIABLE_RELIABILITY_QOS;
cmdQos.history.kind = KEEP_ALL_HISTORY_QOS;
cmdQos.resource_limits.max_samples_per_instance = 1000;
cmdQos.resource_limits.max_instances = 10;
cmdQos.resource_limits.max_samples = 10000;
```

---

## Practical Guidance

- **`KEEP_LAST depth=1` is the right default** for the majority of "current state"
  topics — it bounds memory tightly and matches the common "I only care about the
  latest value" access pattern.
- **Use `KEEP_ALL` deliberately, not by default** — it's required for command/event
  streams where no sample can be skipped, but it demands careful Resource Limits sizing
  to avoid unbounded growth or writer blocking.
- **Always set explicit Resource Limits in production** — default values (often
  `LENGTH_UNLIMITED` in some implementations for certain fields) can mask memory growth
  issues that only surface under load or with a stuck/slow subscriber.
- **Remember History and Resource Limits apply per instance** — a topic with many
  instances (e.g., 10,000 sensors) multiplies memory usage by `depth × instance_count`,
  not just `depth`.
- **Test the "slow/disconnected reader" scenario explicitly** — this is where History
  and Resource Limits choices actually get exercised, and where under-provisioned caps
  cause writer blocking or unexpected sample loss.

---

## Summary

History and Resource Limits together govern DDS's memory footprint and backpressure
behavior when producers outpace consumers. History (`KEEP_LAST` with a depth, or
`KEEP_ALL`) defines the retention *policy* per instance, while Resource Limits
(`max_samples`, `max_instances`, `max_samples_per_instance`) impose hard *caps* that
prevent unbounded memory growth, potentially causing writers to block or reject new
data once exceeded. A key nuance for engineers: `RELIABLE` reliability guarantees
delivery of whatever remains in the writer's history queue, not literally every sample
ever written — achieving true "every sample delivered" semantics requires pairing
`RELIABLE` with `KEEP_ALL` and sufficiently generous Resource Limits. These settings
should always be chosen deliberately per-topic, based on whether the topic represents
"latest state" or "every discrete event" data.

---

## Things to Remember

- **History and Resource Limits apply per instance**, not as a flat global count.
- **`KEEP_LAST depth=1`** = "only the latest value matters" (default, low memory) — good for state/telemetry.
- **`KEEP_ALL`** = "every sample matters, none can be skipped" — required for commands/events, needs careful Resource Limits sizing.
- **`RELIABLE` ≠ "every sample guaranteed"** unless paired with `KEEP_ALL` (or a sufficiently deep `KEEP_LAST`) — `RELIABLE` + shallow `KEEP_LAST` can still skip old samples for a slow reader.
- **`max_samples_per_instance` must be ≥ `history.depth`** for `KEEP_LAST` — inconsistent settings are a configuration error.
- **Always set explicit Resource Limits in production** — don't rely on unbounded defaults, which can hide memory growth until load reveals it.
- **A slow/disconnected reliable reader can cause writer `write()` to block** — this is precisely what Resource Limits + `max_blocking_time` are meant to bound.
