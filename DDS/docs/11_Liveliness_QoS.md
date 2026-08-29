# 11. Liveliness QoS

## Overview

**Liveliness** answers a different question from Deadline (topic 10): not "is this
instance's data fresh?" but **"is this DataWriter (or its DomainParticipant) still
alive and running at all?"** It's DDS's built-in failure-detection mechanism for
publishers — the equivalent of a heartbeat/keepalive system that most broker-based
architectures require you to build yourself, but which DDS provides natively, per
entity, as a QoS contract.

```cpp
qos.liveliness.kind = AUTOMATIC_LIVELINESS_QOS;
qos.liveliness.lease_duration = Duration_t{5, 0}; // must assert liveliness at least every 5s
```

---

## The Core Idea: A Lease Contract

Liveliness works like a **lease**: a DataWriter (or its participant) must "renew the
lease" — assert that it's still alive — at least once every `lease_duration`. If it
fails to do so, DDS considers it **not alive**, and every matched DataReader is notified
via `on_liveliness_changed`, with the corresponding instances typically transitioning to
`NOT_ALIVE_NO_WRITERS` instance state (topic 24) once all writers for that instance lose
liveliness.

This is critical for detecting failures that Deadline alone cannot distinguish: a
DataReader seeing missed deadlines doesn't inherently know whether the writer crashed,
was network-partitioned, or is simply idle by design. Liveliness closes that gap by
giving an explicit, protocol-level "I'm still here" signal independent of whether new
*data* is being published.

---

## The Three Kinds

### `AUTOMATIC_LIVELINESS_QOS` (the default)

- The DDS middleware **itself** automatically asserts liveliness on behalf of the
  **entire DomainParticipant** at a regular interval — no application code required.
- As long as the participant's process is running and its middleware thread is
  scheduled, all DataWriters under it are considered alive, **regardless of whether
  they're actually calling `write()`**.
- Good default for the common case: "I mainly care whether the process/participant
  itself has crashed or been network-partitioned away," not fine-grained per-writer
  liveness.

### `MANUAL_BY_PARTICIPANT_LIVELINESS_QOS`

- The **application** must explicitly call `assert_liveliness()` (on any DataWriter
  under the participant, or a dedicated participant-level call, depending on
  implementation) at least once per `lease_duration`.
- Asserting liveliness on **any one** DataWriter under the participant counts as
  asserting it for **all** DataWriters under that participant using this kind — it's a
  participant-wide assertion, not per-writer.
- Useful when you want liveliness tied to actual application-level health checks (e.g.,
  "my main loop is executing and healthy") rather than purely automatic middleware-level
  heartbeating.

### `MANUAL_BY_TOPIC_LIVELINESS_QOS`

- The **application** must explicitly call `assert_liveliness()` (or `write()`, which
  also counts) on **each specific DataWriter** at least once per `lease_duration`.
- The strictest and most granular option — each DataWriter's liveliness is tracked and
  must be individually maintained.
- Appropriate when different DataWriters within the same participant represent
  genuinely independent responsibilities, and you need to know precisely *which* one
  has stopped functioning, not just that "something in the participant" has an issue.

```cpp
// MANUAL_BY_TOPIC example: writer must explicitly stay alive, write() alone satisfies it
qos.liveliness.kind = MANUAL_BY_TOPIC_LIVELINESS_QOS;
qos.liveliness.lease_duration = Duration_t{2, 0};

// In the application's main loop:
if (hasNewData) {
    writer->write(sample);         // write() counts as an assertion
} else {
    writer->assert_liveliness();   // explicitly renew the lease even with no new data
}
```

---

## Reacting to Liveliness Changes

```cpp
class LivelinessListener : public DataReaderListener {
    void on_liveliness_changed(DataReader* reader,
                                const LivelinessChangedStatus& status) override {
        if (status.alive_count_change < 0) {
            std::cout << "A matched writer just went NOT ALIVE. "
                      << "Currently alive writers: " << status.alive_count << "\n";
            // Trigger failover, alerting, mark data as suspect, etc.
        }
    }
};
```

`LivelinessChangedStatus` reports both the current `alive_count` and
`not_alive_count` of matched writers, plus the deltas since the last event — enabling
reactive logic like triggering failover to a redundant writer (tying directly into
**Ownership** QoS, topic 12) or raising operator alerts.

---

## Compatibility Rule

Liveliness follows the requested-vs-offered model along two dimensions: **kind** (must
be "at least as strong") and **lease duration** (must be "at least as short/frequent"):

```
Kind ordering (weakest → strongest):
AUTOMATIC  <  MANUAL_BY_PARTICIPANT  <  MANUAL_BY_TOPIC

Writer offers kind ≥ reader requests kind, AND
Writer offers lease_duration ≤ reader requests lease_duration
→ compatible
```

For example, a writer offering `MANUAL_BY_TOPIC` with a 1s lease satisfies a reader
requesting `AUTOMATIC` with a 5s lease — but a writer offering `AUTOMATIC` cannot
satisfy a reader requesting `MANUAL_BY_TOPIC`.

---

## Liveliness vs. Deadline vs. Reliability — The Three Failure-Detection Axes

| Policy | Detects | Granularity |
|---|---|---|
| **Reliability** | Individual sample loss | Per sample |
| **Deadline** | Update-rate staleness | Per instance |
| **Liveliness** | Writer/participant existence | Per DataWriter or per DomainParticipant |

A robust real-time system typically uses **all three together**: Reliability ensures
no silent data loss for critical flows, Deadline catches "this specific instance has
gone stale," and Liveliness catches "the producer of this data has disappeared
entirely" — each answering a genuinely different question.

---

## Practical Guidance

- **`AUTOMATIC` is the right default** for most applications — it requires no code and
  correctly detects process crashes/hangs and severe network partitions.
- **Use `MANUAL_BY_TOPIC`** when you have multiple independent DataWriters in one
  participant and need to distinguish "this specific data source failed" from "the
  whole process failed" — common in systems with plugin-like architectures publishing
  many independent feeds from one process.
- **Set `lease_duration` with real margin** above your actual assertion/publish
  interval, and account for network jitter — an overly tight lease causes false
  "not alive" flapping.
- **Pair Liveliness with Ownership QoS** (topic 12) for redundant-publisher failover
  designs: losing liveliness on the currently-exclusive-owner writer is exactly the
  trigger that should promote a backup writer.
- **Don't rely on Deadline alone to infer writer failure** — a writer can go silent for
  reasons unrelated to crashing (e.g., legitimately has nothing new to report); use
  Liveliness for the "is the producer itself gone" question specifically.

---

## Summary

Liveliness QoS is DDS's native failure-detection mechanism for publishers, working as a
renewable lease: a DataWriter or its DomainParticipant must assert it's alive at least
once per `lease_duration`, either automatically (via the middleware, at the participant
level), or explicitly via `assert_liveliness()`/`write()` calls at the participant or
per-DataWriter level, depending on the chosen kind (`AUTOMATIC`,
`MANUAL_BY_PARTICIPANT`, `MANUAL_BY_TOPIC`). Failing to renew the lease notifies matched
readers via `on_liveliness_changed` and transitions affected instances toward
`NOT_ALIVE_NO_WRITERS`. Liveliness is distinct from — and complementary to — Deadline
(which detects per-instance data staleness) and Reliability (which detects per-sample
loss), and is especially important in redundant-publisher failover designs alongside
Ownership QoS.

---

## Things to Remember

- **Liveliness detects "is the writer/participant still alive," not data freshness** — that's Deadline's job.
- **Three kinds**: `AUTOMATIC` (middleware handles it, participant-wide, zero app code — the default), `MANUAL_BY_PARTICIPANT` (app asserts, counts for whole participant), `MANUAL_BY_TOPIC` (app asserts per DataWriter — most granular).
- **`write()` counts as a liveliness assertion** under manual kinds — you don't always need a separate `assert_liveliness()` call if you're actively publishing.
- **Losing liveliness transitions instances toward `NOT_ALIVE_NO_WRITERS`** and fires `on_liveliness_changed` on matched readers.
- **Compatibility requires kind ≥ requested AND lease_duration ≤ requested** — both dimensions must be satisfied.
- **Set lease durations with real margin** to avoid false-positive "not alive" flapping from jitter.
- **Pairs naturally with Ownership QoS** for redundant-writer failover: liveliness loss on the exclusive owner is the failover trigger.
