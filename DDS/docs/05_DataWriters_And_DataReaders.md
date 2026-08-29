# 05. DataWriters and DataReaders

## Overview

**DataWriters** and **DataReaders** are where DDS's data-centric model actually touches
application code — they are the entities you call `write()` and `take()`/`read()` on.
Everything discussed so far (Domain, DomainParticipant, Topic, Publisher, Subscriber)
exists to set up the context in which a DataWriter and a matching DataReader can find
each other and exchange samples according to an agreed-upon contract of QoS.

```
Publisher  → creates → DataWriter  (bound to one Topic)
Subscriber → creates → DataReader  (bound to one Topic)
```

Each DataWriter/DataReader is bound to exactly **one Topic** for its lifetime. Most of
the fine-grained QoS policies that define *how* data is delivered (Reliability,
Durability, History, Deadline, Ownership, Lifespan, Liveliness, Resource Limits, Time-
Based Filter — topics 07–14) are set at this level (or inherited as defaults from the
Topic/Publisher/Subscriber).

---

## DataWriter: Writing Data

The DataWriter's core API surface centers on `write()`, plus explicit instance-lifecycle
operations:

```cpp
DataWriter* writer = publisher->create_datawriter(topic, qos, listener, mask);

VehiclePosition sample{.vehicle_id = 42, .latitude = 48.1, .longitude = 8.4,
                        .speed_kmh = 63.5, .timestamp_ms = now_ms()};

InstanceHandle_t handle = writer->register_instance(sample);  // optional, optimizes repeated writes
writer->write(sample, handle);

// ... later, when this instance no longer exists (e.g., vehicle went offline) ...
writer->dispose(sample, handle);          // marks instance as DISPOSED for all readers
writer->unregister_instance(sample, handle); // writer relinquishes ownership of the instance
```

Key DataWriter operations:

| Operation | Purpose |
|---|---|
| `register_instance()` | Pre-computes/caches the instance handle for a given key value, avoiding repeated key-hashing on every `write()` — an optimization, not strictly required. |
| `write()` | Publishes a new sample/state for an instance. |
| `dispose()` | Explicitly marks an instance as **no longer existing** — propagates `DISPOSED` instance state to all matching readers (distinct from simply stopping writes). |
| `unregister_instance()` | Signals this writer is done writing this instance (relevant for `NO_WRITERS` instance state and Ownership QoS handoff). |
| `get_matched_subscriptions()` | Introspects which DataReaders currently match this writer. |
| `assert_liveliness()` | Manually asserts liveliness when using `MANUAL_BY_TOPIC`/`MANUAL_BY_PARTICIPANT` liveliness QoS (topic 11). |

### `write()` vs. `dispose()` — a frequent point of confusion

Simply *not calling* `write()` anymore does **not** tell subscribers the instance is
gone — it just means no new data arrives (readers may eventually infer staleness via
Deadline QoS, but that's a missed-update signal, not an existence signal). To explicitly
communicate "this instance no longer exists" (e.g., a vehicle was decommissioned, an
order was cancelled), the writer must call `dispose()`. This is one of the most common
correctness bugs in real DDS applications: forgetting to `dispose()` leaves subscribers
with stale-but-technically-still-ALIVE instances forever.

---

## DataReader: Reading Data

DataReaders expose two fundamentally different retrieval semantics: `read()` (non-
destructive) and `take()` (destructive/removing).

```cpp
DataReader* reader = subscriber->create_datareader(topic, qos, listener, mask);

VehiclePositionSeq samples;
SampleInfoSeq infos;

reader->take(samples, infos, LENGTH_UNLIMITED,
             ANY_SAMPLE_STATE, ANY_VIEW_STATE, ANY_INSTANCE_STATE);

for (size_t i = 0; i < samples.length(); i++) {
    if (infos[i].valid_data) {
        std::cout << "vehicle " << samples[i].vehicle_id
                  << " at " << samples[i].latitude << "," << samples[i].longitude << "\n";
    }
}
reader->return_loan(samples, infos); // return middleware-owned buffers
```

| Operation | Behavior |
|---|---|
| `read()` | Returns matching samples but **leaves them in the reader's cache** (marked as READ) — a subsequent `read()` can see them again. |
| `take()` | Returns matching samples and **removes them from the reader's cache** — they will not be returned again. |
| `read_next_sample()` / `take_next_sample()` | Single-sample convenience variants. |
| `get_key_value()` | Recovers the key fields for a given instance handle (useful when you only stored the handle, e.g., from a `DISPOSED` notification with no data payload). |

### Sample, View, and Instance State — the three filtering dimensions

Every retrieved sample carries a `SampleInfo` with three independent state flags that
let you filter precisely what you get back:

| State | Values | Meaning |
|---|---|---|
| **Sample State** | `READ` / `NOT_READ` | Has *this* sample been `read()`/`take()`n before? |
| **View State** | `NEW` / `NOT_NEW` | Is this the *first* sample ever seen for this instance by this reader? |
| **Instance State** | `ALIVE` / `NOT_ALIVE_DISPOSED` / `NOT_ALIVE_NO_WRITERS` | Is the instance currently alive, explicitly disposed, or has it lost all its writers (topic 24)? |

```cpp
// Only get samples for instances that just became newly visible to this reader
reader->take(samples, infos, LENGTH_UNLIMITED,
             ANY_SAMPLE_STATE, NEW_VIEW_STATE, ANY_INSTANCE_STATE);
```

This three-axis filtering is a direct consequence of the data-centric model (topic 01):
DDS isn't just "did a message arrive?" — it's "what is the current state of this
instance, and have I already processed it?"

---

## Listeners vs. WaitSets vs. Polling

DDS supports three complementary ways to consume data and react to status changes —
choosing the right one matters for both responsiveness and thread-model cleanliness.

| Mechanism | Model | Typical use |
|---|---|---|
| **Listener** | Callback invoked by a middleware thread when an event occurs (`on_data_available`, `on_liveliness_changed`, `on_requested_deadline_missed`, etc.) | Simple, event-driven apps; be careful — callbacks run on middleware threads, so keep them fast and non-blocking. |
| **WaitSet + Condition** | Application thread blocks on `wait()` until one or more registered Conditions (e.g., a DataReader's `StatusCondition` or a `ReadCondition`) become true | Apps that want a single, controlled thread pumping multiple entities without per-entity callback threads. |
| **Polling** | Application periodically calls `read()`/`take()` on its own schedule | Simple periodic/cyclic real-time tasks (e.g., a 100 Hz control loop that reads the latest sample each cycle, regardless of whether new data arrived). |

### Example: WaitSet across multiple readers

```cpp
WaitSet waitSet;
StatusCondition* cond1 = positionReader->get_statuscondition();
cond1->set_enabled_statuses(DATA_AVAILABLE_STATUS);
waitSet.attach_condition(cond1);

StatusCondition* cond2 = velocityReader->get_statuscondition();
cond2->set_enabled_statuses(DATA_AVAILABLE_STATUS);
waitSet.attach_condition(cond2);

ConditionSeq active;
waitSet.wait(active, Duration_t{5, 0}); // block up to 5s for any attached condition
for (auto* c : active) {
    // dispatch based on which condition fired
}
```

---

## Matching: When Do a DataWriter and DataReader Actually Connect?

A DataWriter and DataReader **match** (and only then can data flow between them) when
**all** of the following hold:

1. Same **Topic name**.
2. **Compatible data types** (identical, or compatible under XTypes rules — topic 26).
3. **Compatible QoS** — for every "requested vs. offered" QoS policy (topic 15), the
   reader's request must be satisfiable by the writer's offer (e.g., a reader requesting
   `RELIABLE` will **not** match a writer offering only `BEST_EFFORT`).
4. **Overlapping Partitions** (topic 13), if partitions are used.
5. Not blocked by **security/access-control** rules, if DDS Security is enabled
   (topic 32).

When a match/unmatch occurs, both sides receive status events
(`on_subscription_matched` / `on_publication_matched`) — essential for building robust
apps that detect and react to peers appearing or disappearing.

---

## Summary

DataWriters and DataReaders are the entities application code directly interacts with to
publish and consume data — each bound to exactly one Topic, and each carrying the bulk
of DDS's fine-grained QoS configuration. Writing isn't just `write()`: correctly
communicating instance lifecycle requires explicit `dispose()`/`unregister_instance()`
calls, not just silence. Reading isn't just "get the next message": DDS exposes three
orthogonal state dimensions (sample/view/instance state) that let an application query
precisely what it needs — new instances, unread samples, or disposed instances. Consuming
data can be done via listeners (event callbacks), WaitSets (controlled blocking on
multiple conditions), or polling — each suited to different application/threading
models. Ultimately, a writer and reader only exchange data once they **match**: same
topic, compatible type, compatible QoS, overlapping partitions, and (if enabled)
compatible security policy.

---

## Things to Remember

- **Each DataWriter/DataReader binds to exactly one Topic** and carries most of the fine-grained QoS.
- **`write()` publishes; `dispose()` explicitly signals an instance no longer exists** — silence alone does not communicate deletion.
- **`read()` is non-destructive; `take()` removes samples from the reader's cache** — pick deliberately based on whether you need re-readability.
- **SampleInfo exposes three independent states**: sample (read/not-read), view (new/not-new), instance (alive/disposed/no-writers) — use them to filter precisely.
- **Three consumption models**: listeners (callbacks), WaitSets (controlled blocking on conditions), polling (application-driven cadence) — match the model to your threading/real-time needs.
- **Matching requires**: same topic name + compatible type + compatible QoS (requested ≤ offered) + overlapping partitions + (if enabled) compatible security — watch `on_subscription_matched`/`on_publication_matched` to detect it.
- **Forgetting `dispose()`** is one of the most common real-world DDS correctness bugs — don't rely on readers "figuring out" an instance is gone.
