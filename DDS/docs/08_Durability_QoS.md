# 08. Durability QoS

## Overview

**Durability** is the QoS policy that governs DDS's **decoupling in time** (topic 01) —
whether a DataReader that joins the domain *after* samples were published can still
receive relevant past data. Without durability, DDS's publish-subscribe model would be
purely "live only": miss the moment, miss the data, forever. Durability is what lets a
late-joining subscriber catch up.

```cpp
qos.durability.kind = TRANSIENT_LOCAL_DURABILITY_QOS;
```

---

## The Four Kinds

Durability has four levels, each providing progressively stronger persistence
guarantees — and progressively more implementation cost:

| Kind | Where samples are retained | Survives writer restart? | Survives all-process/system restart? |
|---|---|---|---|
| **`VOLATILE`** | Not retained for late joiners at all | No | No |
| **`TRANSIENT_LOCAL`** | In the **writer's own process memory**, for as long as the writer entity exists | No (lost if writer process dies) | No |
| **`TRANSIENT`** | In a **separate, dedicated durability service process** (in memory) | Yes | No (lost on full system/durability-service restart) |
| **`PERSISTENT`** | In the durability service, backed by **disk/database storage** | Yes | Yes (survives full restart, including the durability service itself) |

### `VOLATILE` (the default)

No special handling for late joiners — a DataReader only ever sees samples published
**after** it has matched with the writer. This is the right (and lowest-overhead) choice
for continuously-updating data where a late joiner only cares about the *next* fresh
value anyway (e.g., live telemetry).

### `TRANSIENT_LOCAL`

The writer itself retains the last N samples (per `HISTORY` depth, topic 09) in its own
process memory, and delivers them to any reader that matches **after** those samples
were written — as if the reader had been there all along, up to the retained history
depth. This requires **no additional infrastructure** — it's implemented entirely
within the writer.

This is by far the most commonly used non-volatile durability level in real systems: it
solves the extremely common "late-joining subscriber needs the current configuration/
state, not just future updates" problem without deploying any separate service.

```cpp
// Publisher of a slowly-changing "current configuration" topic
qos.durability.kind = TRANSIENT_LOCAL_DURABILITY_QOS;
qos.history.kind = KEEP_LAST_HISTORY_QOS;
qos.history.depth = 1; // only the latest config value matters
writer->write(currentConfig);

// A subscriber that starts up 10 minutes later still receives currentConfig immediately upon matching
```

**Caveat:** `TRANSIENT_LOCAL` only helps if the **writer process is still alive**. If the
writer has exited, a late joiner gets nothing — for that, you need `TRANSIENT` or
`PERSISTENT`.

### `TRANSIENT`

Adds a **separate, standalone durability/persistence service** (a vendor-specific
process, e.g., RTI's Persistence Service or a similar mechanism in other
implementations) that itself matches as a "reader" of the real writer and re-publishes
the retained data to any late-joining reader — even if the original writer process has
since terminated. Data is kept in memory in this service, so it does **not** survive a
restart of the durability service itself.

### `PERSISTENT`

Same as `TRANSIENT`, but the durability service backs its retained samples with **disk
or database storage**, so data survives even a full restart of the durability service
(and, by extension, the whole system). This is the closest DDS analog to a durable
message queue or Kafka's persisted log — but still applied per-Topic/per-instance, with
full DDS QoS semantics layered on top, rather than being an inherent property of a
central broker.

---

## Durability and Instances — What Actually Gets Delivered

Durability interacts directly with **key-based instances** (topic 24) and **History**
QoS (topic 09):

- What's retained/replayed is bounded by the `HISTORY` depth **per instance**, not a
  flat global buffer — e.g., `TRANSIENT_LOCAL` + `KEEP_LAST` + `depth=1` means "the
  latest sample of every distinct instance (keyed value) seen so far" is what a late
  joiner receives.
- `DISPOSED` instance state is also durable — a late joiner can be told "this instance
  used to exist and was explicitly disposed," not just silently omitted.

---

## Compatibility Rule

Like Reliability, Durability follows the requested-vs-offered model, with an ordering
from weakest to strongest:

```
VOLATILE  <  TRANSIENT_LOCAL  <  TRANSIENT  <  PERSISTENT
```

A writer's offered durability must be **at least as strong** as what the reader
requests:

| Writer offers | Reader requests | Result |
|---|---|---|
| `TRANSIENT_LOCAL` | `VOLATILE` | ✅ Compatible |
| `TRANSIENT_LOCAL` | `TRANSIENT_LOCAL` | ✅ Compatible |
| `VOLATILE` | `TRANSIENT_LOCAL` | ❌ Incompatible — writer doesn't retain late-joiner data |

---

## Practical Guidance

- **`VOLATILE`** — default for high-rate telemetry/sensor data; a late joiner should
  just wait for the next fresh sample rather than replaying stale history.
- **`TRANSIENT_LOCAL`** — the practical default for "current state" / "configuration" /
  "last known value" topics; solves the vast majority of late-joiner needs with zero
  extra infrastructure. This is extremely common in robotics (ROS 2 latched topics map
  to this) and industrial systems.
- **`TRANSIENT` / `PERSISTENT`** — reach for these only when you specifically need data
  to survive the **publishing process itself** disappearing (e.g., an audit trail, a
  mission plan that must survive a ground-station reboot). These require deploying and
  operating an additional durability service — factor that operational cost in.
- **Always pair with a deliberate `HISTORY` depth** — durability defines *whether* data
  persists for late joiners; History defines *how much* per instance.

---

## Summary

Durability QoS governs DDS's decoupling in time: whether late-joining DataReaders can
receive data published before they matched. `VOLATILE` (the default) provides none of
this — late joiners only see future samples. `TRANSIENT_LOCAL` retains recent samples in
the writer's own memory and is the practical, infrastructure-free default for "current
state" topics. `TRANSIENT` and `PERSISTENT` add a separate durability service (in-memory
and disk-backed, respectively) so data survives the original writer — or even the whole
system — restarting, at the cost of additional operational infrastructure. Like other
QoS policies, compatibility follows a requested-vs-offered ordering, and what actually
gets replayed to a late joiner is governed jointly by Durability and History, applied
per keyed instance.

---

## Things to Remember

- **Durability = decoupling in time** — controls whether late-joining readers get past data.
- **Four levels, increasing strength/cost**: `VOLATILE` < `TRANSIENT_LOCAL` < `TRANSIENT` < `PERSISTENT`.
- **`TRANSIENT_LOCAL` is the practical default** for "current state/config" topics — no extra infrastructure needed, but data is lost if the writer process dies.
- **`TRANSIENT`/`PERSISTENT` require a separate durability service** — reach for these only when data must survive the writer (or whole system) restarting.
- **Durability interacts with History** — what's replayed to a late joiner is bounded by History depth, per instance.
- **`DISPOSED` instance state is durable too** — late joiners can learn an instance used to exist and was removed, not just silently miss it.
- **Compatibility is ordered**: a writer must offer durability ≥ what the reader requests, or they won't match.
