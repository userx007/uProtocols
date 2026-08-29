# 07. Reliability QoS

## Overview

**Reliability** is usually the very first QoS policy engineers reach for, and for good
reason: it determines the fundamental delivery guarantee DDS provides for a Topic — do
samples get retransmitted if lost, or not? Unlike broker-based systems where reliability
is often a system-wide or per-queue setting, DDS lets **every single DataWriter/
DataReader pair** independently choose `BEST_EFFORT` or `RELIABLE`, which is one of the
clearest illustrations of DDS's "contract per data flow" philosophy (topic 06).

```cpp
qos.reliability.kind = RELIABLE_RELIABILITY_QOS;   // or BEST_EFFORT_RELIABILITY_QOS
qos.reliability.max_blocking_time = Duration_t{0, 100 * 1000000}; // 100ms
```

---

## The Two Kinds

### `BEST_EFFORT_RELIABILITY_QOS`

- Samples are sent once; if a network packet is lost, **it is never retransmitted**.
- No acknowledgments, no retransmission machinery, no writer-side buffering for
  retransmit purposes.
- Lowest latency and overhead — appropriate when losing an occasional sample is
  acceptable because a **newer** sample will arrive soon anyway (the classic case:
  high-frequency sensor data, where sample N+1 supersedes sample N within milliseconds).
- This is the **default** for `DataReaderQos`/`DataWriterQos` in most implementations —
  always check/set explicitly rather than assuming.

### `RELIABLE_RELIABILITY_QOS`

- DDS guarantees **eventual, in-order delivery** of every sample (bounded by History/
  Resource Limits QoS — topic 09 — which caps how much can be buffered/retransmitted).
- Implemented at the RTPS protocol level via **HEARTBEAT** and **ACKNACK** submessages
  (topic 18): the writer periodically announces the range of sequence numbers it holds;
  readers respond with which sequence numbers they're missing; the writer retransmits
  those specific samples.
- Higher latency and overhead than best-effort — retransmission takes time, and the
  writer must buffer unacknowledged samples (bounded by `RESOURCE_LIMITS`).

---

## Reliability Is Per-Flow, Not Global

This is the detail that most surprises engineers coming from brokers: within a **single
application**, it's completely normal — and often the correct design — to mix
reliability levels across different Topics:

```cpp
// High-rate LIDAR point cloud: losing an occasional frame is fine, latency matters more
lidarQos.reliability.kind = BEST_EFFORT_RELIABILITY_QOS;

// Low-rate mission command: every command MUST arrive
commandQos.reliability.kind = RELIABLE_RELIABILITY_QOS;
```

There is no broker-level or system-level "reliability setting" to reason about — every
Topic's reliability is an independent design decision, made deliberately based on the
nature of that specific data flow.

---

## Compatibility Rule

Reliability follows the standard requested-vs-offered model (topic 06/15):

| Writer offers | Reader requests | Result |
|---|---|---|
| `RELIABLE` | `BEST_EFFORT` | ✅ Compatible (writer exceeds requirement) |
| `RELIABLE` | `RELIABLE` | ✅ Compatible |
| `BEST_EFFORT` | `BEST_EFFORT` | ✅ Compatible |
| `BEST_EFFORT` | `RELIABLE` | ❌ **Incompatible** — writer cannot satisfy the reader's requirement; they do not match |

A very common real-world bug: a reader explicitly (or by leftover default) requests
`RELIABLE`, while the writer is left at its `BEST_EFFORT` default — the two entities
silently fail to match, and no data ever arrives, with no exception thrown. Always check
`on_requested_incompatible_qos` when a reader mysteriously receives nothing.

---

## `max_blocking_time`: Reliable Writers Can Block

A subtlety unique to `RELIABLE` writers: because the writer must retain unacknowledged
samples up to the bound set by `RESOURCE_LIMITS`/`HISTORY`, a `write()` call **can
block** if the writer's queue is full and matched readers haven't caught up yet. The
`max_blocking_time` field of the Reliability QoS caps how long `write()` will block
before returning a `TIMEOUT` error, rather than blocking indefinitely.

```cpp
qos.reliability.max_blocking_time = Duration_t{0, 50 * 1000000}; // 50ms cap
```

This interacts directly with `HISTORY` (`KEEP_LAST` vs `KEEP_ALL`) and
`RESOURCE_LIMITS` (topic 09) — understanding all three together is essential for
avoiding unexpected blocking or dropped writes in production.

---

## How Reliable Delivery Actually Works (Preview of Topic 18)

At a high level, `RELIABLE` reliability is implemented over RTPS via:

1. The writer sends **DATA** submessages for each sample.
2. Periodically (or on demand), the writer sends a **HEARTBEAT** announcing the range of
   sequence numbers currently available.
3. Each reader replies with an **ACKNACK**: acknowledging what it has, and naming any
   gaps (missing sequence numbers).
4. The writer retransmits only the missing samples named in the ACKNACK — not a full
   resend.

This makes DDS reliability closer in spirit to TCP's sliding-window retransmission than
to a broker's "store until consumer acks the whole message" model — but applied
per-sample, per-instance, over (usually) UDP, without a connection-oriented transport
underneath.

---

## Practical Guidance

- **Default to `BEST_EFFORT`** for high-rate, latest-value-wins data (sensor streams,
  telemetry) where an old sample has no value once a newer one exists.
- **Default to `RELIABLE`** for commands, configuration, alarms/events, and anything
  where **every** sample matters and cannot simply be superseded.
- **Always pair `RELIABLE` with a deliberate `HISTORY`/`RESOURCE_LIMITS` choice** — an
  unbounded `KEEP_ALL` reliable writer with a permanently-disconnected reader can
  eventually exhaust memory or block writers.
- **Set `max_blocking_time` explicitly** for any reliable writer in a real-time or
  latency-sensitive path — the default may be longer than acceptable for your control
  loop.
- **Watch for silent mismatches**: if a reader isn't receiving anything, check
  reliability compatibility *before* assuming a network or discovery problem.

---

## Summary

Reliability QoS is the fundamental delivery-guarantee switch in DDS, chosen
independently per DataWriter/DataReader pair rather than globally, embodying DDS's
per-flow contract philosophy. `BEST_EFFORT` sends once with no retransmission — ideal for
high-rate, superseding data where low latency matters more than completeness.
`RELIABLE` guarantees eventual, in-order delivery via RTPS HEARTBEAT/ACKNACK-driven
retransmission, bounded by History/Resource Limits, and can cause `write()` to block
(capped by `max_blocking_time`) when unacknowledged samples accumulate. The
compatibility rule is asymmetric: a `RELIABLE` writer can satisfy a `BEST_EFFORT`
reader, but a `BEST_EFFORT` writer can never satisfy a `RELIABLE` reader — mismatches
here are a leading cause of silent "no data arriving" bugs.

---

## Things to Remember

- **Reliability is set per DataWriter/DataReader**, not globally — mixing reliable and best-effort Topics in one app is normal and expected.
- **`BEST_EFFORT`**: no retransmission, lowest latency — best for high-rate, latest-value-wins data.
- **`RELIABLE`**: guaranteed, in-order eventual delivery via HEARTBEAT/ACKNACK retransmission — best for commands, config, alarms.
- **Compatibility is asymmetric**: `RELIABLE` writer + `BEST_EFFORT` reader = OK; `BEST_EFFORT` writer + `RELIABLE` reader = **incompatible, silent non-match**.
- **`RELIABLE` writers can block on `write()`** when unacknowledged samples fill available resources — always set `max_blocking_time` explicitly.
- **Always pair `RELIABLE` with deliberate History/Resource Limits settings** (topic 09) to avoid unbounded memory growth or unexpected blocking.
- **Check `on_requested_incompatible_qos` first** when a reader unexpectedly receives no data.
