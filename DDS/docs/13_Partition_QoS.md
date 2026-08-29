# 13. Partition QoS

## Overview

**Partition** QoS is a lightweight, string-based mechanism for **logically subdividing
a Domain** without creating new Topics or new Domains. It answers a common operational
need: *"I want to reuse the same Topic name/type across multiple independent groups of
publishers and subscribers, without them crosstalking — but without the heavyweight
isolation of a separate Domain."* Where Domain ID (topic 02) is a hard, protocol-level
boundary, Partition is a soft, runtime-reconfigurable filter applied on top of ordinary
topic matching.

```cpp
PublisherQos pubQos;
pubQos.partition.name.length(1);
pubQos.partition.name[0] = "SimulationA";
```

---

## Where Partition Lives: Publisher/Subscriber Level

Unlike most QoS policies covered so far (Reliability, Durability, History, Deadline,
Liveliness, Ownership — all set on DataWriter/DataReader), **Partition is set on the
Publisher and Subscriber** (topic 04), meaning every DataWriter/DataReader created under
that Publisher/Subscriber shares the same partition membership by default.

```
DomainParticipant
 └── Publisher   [partition = "SimA"]
      └── DataWriter (topic "VehiclePosition")  → effectively publishes into "SimA"
 └── Subscriber  [partition = "SimA"]
      └── DataReader (topic "VehiclePosition")  → only sees "SimA" publishers
```

---

## Matching Rule: Set Intersection, Not Equality

This is the detail that most differentiates Partition from other QoS policies: a
Publisher and Subscriber match on Partition if their **partition name sets have any
overlap at all** — including support for **wildcards** and the special case of **both
having empty partition lists**.

- A Publisher/Subscriber can belong to **multiple partitions simultaneously** (the
  `partition.name` field is a *sequence* of strings, not a single string).
- Wildcards (`*`, `?`) are supported in partition names for flexible matching, following
  POSIX-glob-like syntax in most implementations.
- The **default partition** is the empty string `""`. Two entities both left at default
  (no partition set) match each other normally — Partition is opt-in isolation, not
  something you must configure to get basic connectivity.

```cpp
// Publisher in partitions "Zone1" and "Zone2"
pubQos.partition.name.length(2);
pubQos.partition.name[0] = "Zone1";
pubQos.partition.name[1] = "Zone2";

// Subscriber in partition "Zone2" only → MATCHES (overlap on "Zone2")
subQos.partition.name.length(1);
subQos.partition.name[0] = "Zone2";

// Subscriber in partition "Zone3" only → NO MATCH (no overlap)
```

```cpp
// Wildcard example: Subscriber matches any writer in a partition starting with "Sim"
subQos.partition.name.length(1);
subQos.partition.name[0] = "Sim*";
// Matches writers in "SimA", "SimB", "SimulationRun42", etc.
```

---

## Partition Is Mutable — a Key Operational Advantage

Unlike Reliability, Durability, or History (immutable, fixed at creation — topic 06),
**Partition can be changed at runtime** via `set_qos()` on the Publisher/Subscriber,
without recreating any DataWriters or DataReaders underneath it:

```cpp
PublisherQos qos;
publisher->get_qos(qos);
qos.partition.name.length(1);
qos.partition.name[0] = "SimB"; // dynamically move all this publisher's writers to "SimB"
publisher->set_qos(qos);
```

This makes Partition ideal for **dynamic runtime regrouping** — e.g., moving a
simulation client between simulation runs, dynamically forming ad-hoc subgroups of
robots/vehicles, or toggling a diagnostic subscriber in and out of "see everything"
mode by adding a wildcard partition — all without tearing down and rebuilding the
underlying entities.

---

## Common Use Cases

| Use case | How Partition helps |
|---|---|
| **Multi-tenant simulation** | Multiple independent simulation runs share the same Domain/Topics but never see each other's data — one partition name per run. |
| **Test isolation** | QA/staging traffic tagged with a distinct partition, invisible to production consumers on the same domain. |
| **Dynamic grouping** | Robots/vehicles dynamically join/leave logical "teams" or "zones" at runtime by changing their partition, without reconnecting. |
| **Diagnostic/monitoring tooling** | A monitoring subscriber uses a wildcard partition (`"*"`) to see traffic across all partitions, while normal application subscribers stay scoped to their own partition. |
| **Reusing Topic definitions across environments** | Same Topic name/type used for both "Line1" and "Line2" of a factory, isolated purely by partition rather than duplicating Topic definitions. |

---

## Partition vs. Domain — Choosing the Right Isolation Mechanism

| | **Domain** | **Partition** |
|---|---|---|
| Isolation strength | Hard — different domains never discover each other at the protocol level | Soft — same domain, filtered at the matching layer |
| Granularity | Whole participant (and everything under it) | Per Publisher/Subscriber (can vary per group of writers/readers within one participant) |
| Runtime changeable? | No — Domain ID is fixed at participant creation | Yes — mutable via `set_qos()` |
| Discovery overhead | Separate discovery traffic per domain | Shared discovery traffic; more efficient for many small logical groups |
| Typical use | Environment separation (dev/staging/prod), strict multi-tenancy, security boundary | Logical subgrouping within one environment, dynamic regrouping, test isolation |

**Rule of thumb:** use **Domain** for boundaries that should never, ever cross (security
boundaries, environment separation) and where the overhead of a fully separate discovery
space is acceptable. Use **Partition** for lighter-weight, potentially dynamic, logical
subdivisions within a single operational environment.

---

## Practical Guidance

- **Leave Partition unset (default `""`) unless you have a specific subdivision need** —
  it adds a layer of matching logic that's easy to forget about and can cause confusing
  "why isn't my reader getting data" issues if set inconsistently.
- **Use wildcards for monitoring/diagnostic tooling** that needs cross-partition
  visibility, rather than trying to enumerate every partition name a diagnostic
  subscriber might need.
- **Don't use Partition as a security boundary** — it's a convenience/filtering
  mechanism, not an access-control mechanism; anyone who can set an arbitrary partition
  string can potentially see data in that partition (use DDS Security, topic 32, for
  actual access control).
- **Document your partition naming scheme** — since it's just free-form strings with
  glob matching, a lack of naming conventions across teams leads to accidental overlaps
  or accidental isolation.

---

## Summary

Partition QoS provides a lightweight, string-based, runtime-mutable mechanism for
logically subdividing a Domain, set on the Publisher/Subscriber (not the DataWriter/
DataReader) and inherited by everything created underneath. Unlike Domain's hard,
protocol-level isolation, Partition matching works by **set intersection** (with
wildcard support) between a Publisher's and Subscriber's partition name lists —
entities with any overlapping partition name (or both left at the empty-string default)
match normally. Because Partition is mutable at runtime, it's well suited for dynamic
regrouping scenarios (multi-tenant simulations, ad-hoc team/zone membership, diagnostic
tooling with wildcard visibility) that would otherwise require tearing down and
recreating entities. It should be chosen over a separate Domain when isolation needs are
lighter-weight or need to change dynamically, but should never be relied upon as a
security boundary.

---

## Things to Remember

- **Partition is set on Publisher/Subscriber**, not DataWriter/DataReader — inherited by everything created underneath.
- **Matching is by set intersection with wildcard support**, not equality — any overlapping partition name (including both empty-string defaults) allows a match.
- **Partition is mutable at runtime** via `set_qos()` — ideal for dynamic regrouping without recreating entities.
- **Default partition is `""`** — Partition is opt-in isolation; leaving it unset doesn't break basic connectivity.
- **Use Domain for hard/security boundaries; use Partition for lighter-weight, potentially dynamic logical subdivision** within one environment.
- **Never use Partition as an access-control/security mechanism** — it's a convenience filter, not enforced security (use DDS Security, topic 32, for that).
- **Wildcards (`*`, `?`) are extremely useful for monitoring/diagnostic tooling** that needs cross-partition visibility.
