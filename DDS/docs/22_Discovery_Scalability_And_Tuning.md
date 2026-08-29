# 22. Discovery Scalability and Tuning

## Overview
DDS's default discovery (SPDP + SEDP) is simple and robust for small-to-medium deployments, but it scales poorly by design: every participant talks to every other participant, and every endpoint's metadata is exchanged with every other participant. This document covers why that happens and the standard mitigation strategies.

## The O(n²) Problem
With `N` participants:
- Each participant sends periodic SPDP announcements that all `N-1` others must receive and process.
- Each participant's SEDP endpoints (`M` per participant on average) are announced to all other participants, and reliable delivery requires ACKNACK handshakes per remote reader.

Total discovery traffic and CPU cost grows roughly with `N²` (participants) and `N² * M` (endpoints), meaning that doubling the number of participants can roughly **quadruple** discovery overhead — this becomes untenable well before 100+ participants on a flat domain in many real systems, especially with resource-constrained or high-latency links.

## Mitigation Strategies

### 1. Discovery Servers (Centralized Discovery)
Instead of full mesh multicast discovery, participants act as **clients** of one or more **discovery server** participants. Clients only talk to the server(s); the server aggregates and redistributes discovery info.
```
[Client A] --unicast--> [Discovery Server] <--unicast-- [Client B]
```
This turns discovery traffic from O(n²) into roughly O(n), at the cost of the server(s) becoming a scaling/availability bottleneck (mitigated by running redundant servers).

### 2. Initial Peers List (No Multicast)
In environments where multicast is unavailable (cloud VPCs, WAN links), configure a static list of unicast peer addresses so SPDP doesn't rely on multicast discovery at all:
```xml
<discovery>
  <initial_peers>
    <element>10.0.1.5</element>
    <element>10.0.1.6</element>
  </initial_peers>
</discovery>
```

### 3. Static Endpoint Discovery
Skip SEDP entirely by pre-configuring endpoint GUIDs, locators, and QoS out-of-band (typically via XML). Eliminates SEDP traffic completely but sacrifices flexibility — any endpoint change requires redeploying config.

### 4. Domain / Partition Segmentation
Split a large system into multiple Domain IDs or use `Partition` QoS to logically segment traffic so that not every participant needs to discover every other one — participants outside a partition/domain simply never see each other's endpoints.

### 5. Tuning Announcement Periods
Lengthening SPDP announcement intervals and lease durations reduces steady-state background traffic once the system is stable, at the cost of slower failure detection.

## Example: Discovery Server Configuration (RTI Connext style)
```bash
# Start a discovery server on domain 0
rtiddsspy -domainId 0

# Or, in code:
DomainParticipantQos qos;
qos.discovery_config.discovery.initial_peers.push_back("rtps@udpv4://10.0.1.10:7400");
qos.discovery_config.builtin_discovery_plugins = DISCOVERYPLUGIN_REMOTE;
```

## Comparison Table

| Strategy | Discovery Traffic | Flexibility | Failure Mode |
|---|---|---|---|
| Default multicast SPDP/SEDP | O(n²) | High | Multicast storms, flapping |
| Discovery Server | ~O(n) | High | Server SPOF (mitigate w/ redundancy) |
| Static Initial Peers | O(n) unicast | Medium | Manual peer list maintenance |
| Static Endpoint Discovery | ~O(1) after startup | Low | Config drift on changes |

## Key Takeaways
- Default DDS discovery is O(n²) and becomes a real bottleneck in large or WAN-spanning deployments.
- Discovery servers are the most common production fix, trading a small SPOF risk for near-linear scaling.
- When multicast isn't available (cloud, VPN, WAN), initial peers lists are mandatory, not optional.
- Static discovery trades runtime flexibility for near-zero discovery overhead — best for fixed, embedded, or safety-critical topologies.
- Segmenting via Domain ID or Partitions reduces the *effective* N that any one participant must discover.
