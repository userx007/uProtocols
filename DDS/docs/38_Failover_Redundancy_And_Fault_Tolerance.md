# 38. Failover, Redundancy and Fault Tolerance Patterns

## Overview
DDS's decentralized architecture gives it strong natural building blocks for fault-tolerant systems — no broker means no single point of failure at the messaging layer itself. This document ties together three QoS mechanisms covered earlier — **Ownership/Ownership Strength** (topic 12), **Liveliness** (topic 11), and multi-writer patterns — into concrete failover and redundancy designs.

## Pattern 1: Hot/Warm Standby via Ownership Strength
The most common DDS redundancy pattern: multiple writers publish the **same keyed instance**, with `EXCLUSIVE` ownership and different `OwnershipStrength` values. Readers only "see" data from the highest-strength **live** writer; if that writer fails (liveliness lost), the next-highest-strength writer's data becomes visible automatically — no application-level failover logic required.

```cpp
// Primary controller
writerQos.ownership.kind = EXCLUSIVE_OWNERSHIP_QOS;
writerQos.ownership_strength.value = 100;   // highest strength = primary

// Backup controller
writerQos.ownership.kind = EXCLUSIVE_OWNERSHIP_QOS;
writerQos.ownership_strength.value = 50;    // lower strength = standby
```
```
Normal operation:  Reader sees data from Primary (strength 100)
Primary fails:     Liveliness lost -> Reader automatically starts seeing Backup (strength 50)
Primary recovers:  Reader automatically switches back to Primary (strength 100) once it re-announces
```
This is entirely middleware-managed — the standby writer can even be actively publishing the whole time (hot standby) and simply be ignored by readers until needed, achieving zero-downtime failover.

## Pattern 2: Liveliness-Based Failure Detection
`LivelinessQosPolicy` (topic 11) determines how quickly a failed writer is actually detected:
```cpp
writerQos.liveliness.kind = MANUAL_BY_TOPIC_LIVELINESS_QOS;
writerQos.liveliness.lease_duration = Duration(1, 0);  // 1 second
```
Faster lease durations mean faster failover but more assertion traffic and higher risk of false-positive failover under transient network jitter — this is a direct trade-off engineers must tune per application criticality.

## Pattern 3: Redundant Discovery Infrastructure
As discussed in topic 22, centralized discovery servers (used for scalability) reintroduce a potential single point of failure — mitigated by running **multiple discovery servers** that clients connect to redundantly:
```xml
<discovery_config>
  <discovery>
    <initial_peers>
      <element>rtps@udpv4://10.0.1.10:7400</element>  <!-- Discovery Server 1 -->
      <element>rtps@udpv4://10.0.1.11:7400</element>  <!-- Discovery Server 2 -->
    </initial_peers>
  </discovery>
</discovery_config>
```
Clients maintain connections to both; loss of one discovery server doesn't halt new discovery, only reduces redundancy until it's restored.

## Pattern 4: Durability for State Recovery
`TRANSIENT_LOCAL` or `TRANSIENT`/`PERSISTENT` Durability QoS (topic 8) ensures that a **restarted** or **newly-joined** reader can recover last-known state without needing a separate out-of-band state-sync mechanism — critical for failover scenarios where a backup component needs to "catch up" instantly upon activation.

## Example: Combining Patterns for a Redundant Control System
```
[Primary Controller] --EXCLUSIVE, strength=100, TRANSIENT_LOCAL--\
                                                                    +--> [State Topic] --> [Actuator Reader]
[Backup Controller]  --EXCLUSIVE, strength=50,  TRANSIENT_LOCAL --/

Both controllers publish continuously (hot standby).
Actuator reader always sees Primary's data while Primary is alive.
On Primary failure (liveliness lease expires), Actuator reader
  automatically and transparently starts receiving Backup's data
  — with TRANSIENT_LOCAL ensuring even a reader that (re)joins
  mid-failover gets the latest state immediately, not just future updates.
```

## Common Pitfalls
- Setting overly aggressive liveliness lease durations without load-testing under real network jitter — causes false-positive failovers ("flapping") between primary and backup.
- Forgetting that `EXCLUSIVE` ownership arbitration is **per-instance** (topic 24) — a multi-instance topic needs every instance's strength values considered independently, not just a single "global" primary/backup assumption.
- Relying on a single discovery server without redundancy, reintroducing the exact SPOF that DDS's decentralized model was meant to avoid.
- Not testing the "recovery" direction (primary comes back online) — some systems handle failover correctly but have bugs in fail-back logic, causing unexpected oscillation or a permanently "stuck" backup.

## Key Takeaways
- `EXCLUSIVE` Ownership + `OwnershipStrength` is the core DDS pattern for automatic, middleware-managed hot/warm standby failover — no custom application failover logic required.
- Liveliness lease duration directly controls failover speed vs. false-positive risk — tune deliberately, don't leave at defaults for critical systems.
- Discovery infrastructure (if centralized for scalability) needs its own redundancy plan to avoid reintroducing a single point of failure.
- Durability QoS (`TRANSIENT_LOCAL`/`TRANSIENT`/`PERSISTENT`) ensures recovering or newly-joined components get immediate state, which is essential for fast, correct failover.
- Always test both failover *and* fail-back directions, and test under realistic network jitter, not just clean shutdown scenarios.
