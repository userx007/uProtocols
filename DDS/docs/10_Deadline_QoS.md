# 10. Deadline QoS

## Overview

**Deadline** is DDS's built-in mechanism for detecting **staleness** — it answers the
question "has this instance been updated recently enough?" rather than "was this
specific sample delivered?" (which is Reliability's job, topic 07). Deadline is a
contract about the **rate** of updates a DataWriter promises and a DataReader expects,
per instance, and DDS actively monitors and reports violations of that contract.

```cpp
qos.deadline.period = Duration_t{1, 0}; // must write/receive an update at least every 1 second
```

---

## The Core Idea: A Promised Update Period

Deadline QoS declares: **"for each instance of this topic, a new sample must arrive at
least once every `period`."** This is fundamentally different from Reliability, which
concerns whether individual samples are lost; Deadline concerns whether the **rate** of
updates is being honored, regardless of whether any individual sample was lost.

- On the **DataWriter** side: the writer promises (offers) to publish updates for each
  instance at least that often.
- On the **DataReader** side: the reader requires (requests) that it receive updates at
  least that often, for every instance it's tracking.

If either side fails to honor this — the writer doesn't call `write()` for an instance
within the period, or the reader doesn't receive a sample for an instance within the
period — DDS raises a status event:

- **Writer side**: `on_offered_deadline_missed` — "I (the writer) failed to update this
  instance in time."
- **Reader side**: `on_requested_deadline_missed` — "I (the reader) have not received an
  update for this instance in time" (this fires regardless of *why* — network loss,
  writer failure, writer simply not producing new data, etc.).

---

## Example: heartbeat/liveness-style monitoring via Deadline

```cpp
// Vehicle telemetry writer promises an update at least every 500ms per vehicle instance
DataWriterQos writerQos;
writerQos.deadline.period = Duration_t{0, 500 * 1000000}; // 500ms

// Fleet monitoring reader requires the same guarantee
DataReaderQos readerQos;
readerQos.deadline.period = Duration_t{0, 500 * 1000000};

class DeadlineListener : public DataReaderListener {
    void on_requested_deadline_missed(DataReader* reader,
                                       const RequestedDeadlineMissedStatus& status) override {
        // status.last_instance_handle identifies WHICH instance went stale
        InstanceHandle_t stale = status.last_instance_handle;
        VehiclePosition key;
        reader->get_key_value(key, stale);
        std::cout << "Vehicle " << key.vehicle_id << " hasn't reported in 500ms!\n";
        // Trigger an alert, mark the vehicle as "possibly offline", etc.
    }
};
```

This pattern is extremely common for **liveness/health monitoring** of many independent,
keyed entities (vehicles, sensors, nodes) without writing any custom timeout logic —
DDS does the per-instance timing and notification natively.

---

## Deadline vs. Liveliness — Don't Confuse Them

Both Deadline and **Liveliness** (topic 11) detect "something has gone quiet," but at
different granularities and for different purposes:

| | **Deadline** | **Liveliness** |
|---|---|---|
| Granularity | **Per instance** (per keyed value within a Topic) | **Per entity** (per DataWriter, or per Participant) |
| Detects | An instance hasn't been updated within its promised period | A writer (or its whole participant) has stopped asserting it's alive at all |
| Typical use | "Has this specific vehicle's position gone stale?" | "Is this publisher process still running at all?" |
| Assertion mechanism | Any `write()` call on that instance resets its deadline timer | `write()`, explicit `assert_liveliness()`, or automatic protocol-level heartbeating, depending on liveliness kind |

A writer can be perfectly "alive" (Liveliness-wise) while still missing Deadlines for
specific instances (e.g., it's running fine but simply hasn't had new data for instance
X in a while) — the two policies are complementary, not redundant.

---

## Compatibility Rule

Deadline follows the requested-vs-offered model, but the comparison is **numeric period
length**, not a categorical ordering:

```
Writer offers period ≤ Reader requests period → compatible (writer updates at least as often as required)
Writer offers period >  Reader requests period → INCOMPATIBLE
```

```cpp
// Writer offers updates every 200ms; reader only requires every 1s → compatible (writer exceeds requirement)
writerQos.deadline.period = Duration_t{0, 200 * 1000000};
readerQos.deadline.period = Duration_t{1, 0};
```

---

## Practical Uses

- **Health/liveness monitoring of many keyed entities** without custom timeout code —
  fleet vehicles, IoT sensors, distributed service heartbeats represented as DDS
  instances.
- **Real-time control loop watchdogs** — detecting a sensor feed has gone stale before
  a control algorithm acts on outdated data (critical in safety-relevant systems).
- **SLA-style monitoring between independently-developed publishers and subscribers** —
  the reader's Deadline QoS *is* a declared, middleware-enforced expectation, not just
  application-level polling logic scattered across the codebase.

---

## Practical Guidance

- **Set the period with realistic margin** above the writer's actual publish rate —
  e.g., if a writer publishes every 100ms, a reader deadline of exactly 100ms will
  trigger spurious missed-deadline events on minor jitter; 150–200ms gives headroom.
- **Handle `on_requested_deadline_missed` explicitly** — it's easy to configure Deadline
  QoS and forget to actually *do* anything meaningful when it fires; without a listener
  or condition handling it, the status is silently updated but ignored.
- **Combine with Liveliness for full failure coverage** — Deadline alone won't tell you
  *why* an instance went stale (writer crashed vs. simply idle); Liveliness on the
  writer side helps distinguish "writer is gone" from "writer is alive but this
  particular instance stopped updating."
- **Remember it's per-instance** — a single DataReader tracking 500 vehicle instances
  gets independent deadline tracking for each of the 500, not one aggregate timer.

---

## Summary

Deadline QoS is a per-instance contract about update **rate**, distinct from
Reliability's concern with individual sample delivery — it lets a DataWriter promise
"I'll update each instance at least this often" and a DataReader require the same,
with DDS actively monitoring and firing `on_offered_deadline_missed` /
`on_requested_deadline_missed` status events on violation. It's commonly used to build
liveness/health monitoring for large numbers of independently-keyed entities (vehicles,
sensors, nodes) without any custom timeout logic, and pairs naturally with Liveliness
QoS (which detects entity-level, not instance-level, disappearance). As with other QoS
policies, compatibility follows a requested-vs-offered model — here, the writer's
offered period must be no longer than the reader's requested period.

---

## Things to Remember

- **Deadline is per-instance**, not per-DataWriter/DataReader as a whole — each keyed instance gets independent timing.
- **It detects staleness (missed update rate), not sample loss** — that's Reliability's job.
- **Any `write()` on an instance resets its deadline timer**; missing the period fires `on_offered_deadline_missed` (writer) or `on_requested_deadline_missed` (reader).
- **Deadline ≠ Liveliness** — Deadline is per-instance update-rate monitoring; Liveliness (topic 11) is per-entity "is this writer/participant still alive at all" monitoring.
- **Compatibility**: writer's offered period must be ≤ reader's requested period.
- **Always implement the missed-deadline listener/handling** — configuring the QoS without reacting to the event leaves the feature inert.
- **Set realistic margin above actual publish rate** to avoid spurious missed-deadline noise from normal jitter.
