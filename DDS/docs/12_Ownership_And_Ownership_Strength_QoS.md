# 12. Ownership and Ownership Strength QoS

## Overview

**Ownership** QoS answers a question that only becomes relevant once you have
**redundant publishers** writing to the same instance: *when multiple DataWriters
publish samples for the same keyed instance, whose data does a DataReader actually
see?* This is one of DDS's most powerful — and most misunderstood — features, because
it enables **hot-standby redundancy** for critical data flows natively, without any
application-level arbitration logic.

```cpp
qos.ownership.kind = EXCLUSIVE_OWNERSHIP_QOS; // or SHARED_OWNERSHIP_QOS
qos.ownership_strength.value = 10;            // only meaningful for EXCLUSIVE
```

---

## The Two Kinds

### `SHARED_OWNERSHIP_QOS` (the default)

- Multiple DataWriters may publish to the **same instance**, and a DataReader receives
  samples from **all of them**, interleaved, in whatever order they arrive.
- No arbitration — the application must handle the fact that different writers might
  disagree about the instance's current value, if that's even a meaningful scenario for
  the data in question.
- Appropriate when multiple legitimate sources genuinely contribute *different*
  information about the same instance, or when redundant-writer conflict simply isn't a
  concern for that Topic.

### `EXCLUSIVE_OWNERSHIP_QOS`

- For any given instance, only **one DataWriter** — the current **owner** — has its
  samples delivered to matching DataReaders at a time. Samples from all other writers
  for that same instance are **suppressed** (not delivered) as long as the owner remains
  alive.
- The owner is determined by **Ownership Strength**: the DataWriter with the highest
  `ownership_strength.value` among currently-live writers for that instance is the
  owner.
- If the current owner **loses liveliness** (topic 11) or stops matching, ownership
  **automatically fails over** to the next-highest-strength live writer — this handoff
  is entirely middleware-managed, with no application-level voting or coordination
  protocol required.

---

## Why This Matters: Native Hot-Standby Redundancy

This is the headline use case for Exclusive Ownership: **N redundant publishers, one
"active" at a time, automatic failover on failure** — a pattern that would otherwise
require a custom leader-election protocol (e.g., built on top of a broker or a separate
consensus system).

```cpp
// Primary controller — highest strength, normally the active data source
DataWriterQos primaryQos;
primaryQos.ownership.kind = EXCLUSIVE_OWNERSHIP_QOS;
primaryQos.ownership_strength.value = 100;
primaryQos.liveliness.kind = AUTOMATIC_LIVELINESS_QOS;
primaryQos.liveliness.lease_duration = Duration_t{2, 0};

// Backup controller — lower strength, only "wins" if the primary goes silent
DataWriterQos backupQos;
backupQos.ownership.kind = EXCLUSIVE_OWNERSHIP_QOS;
backupQos.ownership_strength.value = 50;
backupQos.liveliness.kind = AUTOMATIC_LIVELINESS_QOS;
backupQos.liveliness.lease_duration = Duration_t{2, 0};

// Reader requesting EXCLUSIVE ownership sees ONLY the primary's data...
// ...until the primary's liveliness lapses, at which point the backup's data
// begins flowing automatically — no reconfiguration, no reconnection needed.
```

This pattern is extremely common in **industrial control, avionics, and redundant
sensor fusion systems**, where a primary and one or more hot standbys publish to the
same logical instance (e.g., `"ControlSetpoint"` for a given actuator, keyed by
`actuator_id`), and the system must fail over within milliseconds without any
supervisory process making a decision.

---

## Dynamic Strength Adjustment

`ownership_strength` is a **mutable** QoS field (topic 06) — it can be changed at
runtime via `set_qos()`, enabling dynamic re-prioritization:

```cpp
// Application-level health check determines this writer should become primary
DataWriterQos qos;
writer->get_qos(qos);
qos.ownership_strength.value = 200; // now outranks the previous primary
writer->set_qos(qos);
```

This allows building more sophisticated failover logic than pure liveliness-based
handoff — e.g., an application-level health/quality score that dynamically promotes
whichever redundant source currently has the best signal quality, sensor confidence, or
computed health score, with DDS handling the actual data routing based on the current
strength ranking.

---

## Ownership Is Per-Instance, Not Per-Topic

Like History and Deadline, Ownership arbitration happens **independently per keyed
instance** (topic 24). Two different writers can each be the "owner" of *different*
instances of the same Topic simultaneously:

```
Topic: ActuatorSetpoint (keyed by actuator_id)
  Instance actuator_id=1 → owned by WriterA (strength 100)
  Instance actuator_id=2 → owned by WriterB (strength 100)
```

This means Exclusive Ownership isn't just "one global writer per topic" — it's a
fine-grained, per-instance arbitration mechanism, which matters a great deal in systems
with many independently-owned entities.

---

## Compatibility Rule

Ownership kind must **match exactly** — unlike most other QoS policies, this is not an
"offered ≥ requested" ordering:

```
Writer offers EXCLUSIVE + Reader requests EXCLUSIVE → compatible
Writer offers SHARED    + Reader requests SHARED    → compatible
Writer offers EXCLUSIVE + Reader requests SHARED    → INCOMPATIBLE
Writer offers SHARED    + Reader requests EXCLUSIVE → INCOMPATIBLE
```

All DataWriters that write to the same instance must also agree on the **same**
Ownership kind — mixing `SHARED` and `EXCLUSIVE` writers for the same instance is an
error condition in most implementations.

---

## Practical Guidance

- **Default to `SHARED`** unless you have a specific redundant-writer arbitration need —
  it's simpler and matches most data flows where only one legitimate writer exists per
  instance anyway.
- **Use `EXCLUSIVE` for hot-standby redundancy** — primary/backup controllers, redundant
  sensor sources feeding a single logical value, or any scenario needing automatic,
  middleware-managed failover without custom leader election.
- **Always pair `EXCLUSIVE` Ownership with well-tuned Liveliness** — the failover
  trigger *is* liveliness loss, so an overly long `lease_duration` directly delays
  failover, while an overly short one risks spurious failover on jitter.
- **Remember arbitration is per-instance** — design your key fields with this in mind if
  you need independent redundancy per logical entity (e.g., per actuator, per zone,
  per subsystem).
- **Test the actual failover path**, not just steady-state behavior — verify the
  backup's data really does start flowing within your required time budget once the
  primary's liveliness lapses.

---

## Summary

Ownership QoS resolves what happens when multiple DataWriters publish to the same keyed
instance: `SHARED` (the default) delivers all writers' samples to readers with no
arbitration, while `EXCLUSIVE` designates a single current **owner** — the live writer
with the highest `ownership_strength` — whose samples alone are delivered, with
automatic, middleware-managed failover to the next-highest-strength writer when the
owner loses liveliness. This is DDS's native mechanism for hot-standby redundancy,
requiring no custom leader-election logic, and `ownership_strength` can be adjusted
dynamically at runtime to support application-driven failover decisions. Ownership
arbitration is applied per-instance, not per-topic, and — unlike most QoS policies —
its compatibility rule requires an exact kind match between writer and reader, not an
ordering.

---

## Things to Remember

- **`SHARED` (default)**: all writers' samples delivered, no arbitration.
- **`EXCLUSIVE`**: only the highest-`ownership_strength`, currently-live writer's samples are delivered per instance — automatic failover on liveliness loss.
- **Ownership arbitration is per-instance**, not per-topic — different instances of the same topic can have different current owners.
- **Failover is triggered by Liveliness loss** (topic 11) — tune lease duration deliberately to balance failover speed vs. false-positive risk.
- **`ownership_strength` is mutable at runtime** via `set_qos()`, enabling dynamic, application-driven re-prioritization of redundant sources.
- **Compatibility requires an exact kind match** (`EXCLUSIVE`↔`EXCLUSIVE`, `SHARED`↔`SHARED`) — not an "offered ≥ requested" ordering like most other policies.
- **All writers of the same instance must agree on the same Ownership kind** — mixing `SHARED` and `EXCLUSIVE` writers for one instance is invalid.
