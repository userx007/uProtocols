# 21. Simple Endpoint Discovery Protocol (SEDP)

## Overview
Once SPDP has established that two `DomainParticipant`s know about each other, SEDP is the second tier of RTPS discovery: it exchanges information about the actual **DataWriters and DataReaders** (endpoints) each participant hosts, so that writer/reader pairs on matching topics can find and connect to each other.

## Core Idea
SEDP uses its own set of built-in topics/endpoints:
- `DCPSPublication` — announces DataWriters
- `DCPSSubscription` — announces DataReaders
- `DCPSTopic` (vendor-optional) — announces Topic definitions

Each announcement contains everything needed to decide whether a match is possible:
- Topic name and type name
- Full QoS policy set (reliability, durability, deadline, ownership, etc.)
- The endpoint's GUID
- Partition membership
- Content filter expressions (for `ContentFilteredTopic`s)

## How It Works, Step by Step
1. After SPDP completes, participants use each other's discovered unicast locators to exchange SEDP data — **reliably**, unlike the best-effort SPDP announcements.
2. Each side's built-in SEDP DataWriter publishes information about local user-created DataWriters/DataReaders.
3. The receiving participant's discovery listener inspects each announcement and applies the **matching algorithm**:
   - Topic name must match exactly.
   - Type must be compatible (identical, or compatible under XTypes rules).
   - QoS must be compatible (see topic 15 — request vs. offered).
   - Partition strings must intersect (see topic 13).
4. If all criteria pass, the middleware creates an internal **match** — after which actual user data (DATA submessages) can begin flowing between that writer and reader pair.
5. SEDP re-announces on QoS changes (e.g., a writer's liveliness or deadline QoS is updated at runtime) so peers can re-evaluate compatibility.

## Example: Why a Reader Doesn't See a Writer's Data
Imagine a writer publishes on topic `VehicleStatus` with `RELIABLE` reliability, and a reader subscribes to the same topic but requests `RELIABLE` too — that matches. But if the reader's `Partition` QoS is `"Fleet_A"` and the writer's is `"Fleet_B"`, SEDP will discover both endpoints yet **refuse to match them**, because partitions form disjoint discovery groups even on an identical topic name/type.

```
Writer: topic=VehicleStatus, partition=["Fleet_B"], reliability=RELIABLE
Reader: topic=VehicleStatus, partition=["Fleet_A"], reliability=RELIABLE
Result: Discovered by SEDP, but NOT matched (partition mismatch)
```

## Example: SEDP Traffic Growth
For `N` participants each with `M` endpoints, every participant must receive and process SEDP announcements from every other participant's endpoints — contributing to the overall O(n²) discovery cost alongside SPDP (this is elaborated further in topic 22).

## Common Pitfalls
- Assuming topic name matching alone is sufficient — a huge source of "why won't my writer/reader connect" bugs is QoS or partition mismatch, not name/type mismatch.
- Forgetting that SEDP announcements are **reliable**, so they consume real bandwidth and retransmission overhead proportional to the number of endpoints, not just participants.
- Not accounting for SEDP re-announcement storms when QoS is changed dynamically on many endpoints at once.

## Key Takeaways
- SEDP is the **endpoint-level** discovery layer, running after SPDP has found the participants.
- It exchanges topic name, type, QoS, and partition info via reliable built-in DataWriters/DataReaders.
- Matching requires: topic name match + type compatibility + QoS compatibility + partition intersection.
- Discovery ≠ Matching: participants/endpoints can be *discovered* without ever being *matched*.
- SEDP traffic scales with the number of endpoints, making it a key lever in discovery scalability tuning.
