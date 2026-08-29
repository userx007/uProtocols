# 28. Multicast vs Unicast Delivery

## Overview
Once writers and readers are matched, DDS still has a choice for *how* to physically send each sample: as one packet per reader (**unicast**) or as one packet shared by all interested readers on the network segment (**multicast**). This choice has major implications for network load, switch/router behavior, and scalability with fan-out.

## Unicast Delivery
Each sample is sent as an individually-addressed UDP (or TCP) packet to every matched reader's locator.
```
Writer --> [packet copy 1] --> Reader A
Writer --> [packet copy 2] --> Reader B
Writer --> [packet copy 3] --> Reader C
```
- **Pros**: Works everywhere, including networks where multicast is disabled (cloud VPCs, WAN links); simpler to reason about and firewall.
- **Cons**: Network and CPU load scale linearly with the number of readers — a writer with 100 readers sends 100 copies of every sample.

## Multicast Delivery
The writer sends **one** packet to a multicast group address; the network (switches/routers with IGMP snooping, PIM, etc.) replicates it only to segments with interested subscribers.
```
Writer --> [1 packet] --> Multicast Group 239.255.1.5 --> Reader A, B, C (all receive the same packet)
```
- **Pros**: Dramatically reduces writer-side send load and network bandwidth for high fan-out (many readers on one topic) — the classic use case is broadcasting sensor or state data to many consumers.
- **Cons**: Requires multicast-capable network infrastructure (properly configured IGMP snooping/querier, PIM for cross-subnet routing); often disabled or unreliable in cloud environments, VPNs, and some enterprise networks; harder to firewall/secure cleanly; UDP multicast is inherently best-effort, so RELIABLE QoS still needs unicast-based ACKNACK/retransmission on top.

## Example: QoS Configuration
Most DDS vendors allow per-endpoint control over which delivery mode to prefer:
```xml
<datawriter_qos>
  <multicast>
    <value>
      <element>
        <receive_address>239.255.1.5</receive_address>
        <receive_port>7411</receive_port>
      </element>
    </value>
  </multicast>
</datawriter_qos>
```
And on the reader side, requesting to receive via multicast:
```xml
<datareader_qos>
  <multicast>
    <value>
      <element>
        <receive_address>239.255.1.5</receive_address>
        <receive_port>7411</receive_port>
      </element>
    </value>
  </multicast>
</datareader_qos>
```

## Example: Hybrid Approach for Reliable Multicast
Best-effort data can go out over multicast for efficient fan-out, but the retransmission of *missed* samples (detected via HEARTBEAT/ACKNACK, topic 18) is typically done via **unicast** back to the specific reader that reported the gap — combining multicast's fan-out efficiency with unicast's precision for repair traffic. Some vendors also support multicast-based retransmission for very large reader counts, at the cost of "over-delivering" repairs to readers that didn't need them.

## Decision Guide

| Scenario | Recommended Mode |
|---|---|
| Few readers (1–5), point-to-point | Unicast |
| Many readers (dozens+) on a well-configured LAN | Multicast |
| Cloud VPC / Kubernetes / WAN | Unicast (multicast often unsupported) |
| High-frequency, high-fan-out sensor broadcast | Multicast, with unicast repair |
| Security-sensitive / needs per-reader firewall rules | Unicast |

## Common Pitfalls
- Assuming multicast is always more efficient — with only 1–2 readers, multicast adds complexity (IGMP dependency) for no real gain over unicast.
- Deploying multicast-reliant configurations into cloud/container environments without verifying multicast support first — a very common source of "works on my laptop, fails in production" bugs.
- Not accounting for switch-level multicast flooding when IGMP snooping isn't properly configured — turning "efficient" multicast into a broadcast storm.
- Overlooking that RELIABLE QoS's repair traffic is often unicast even when the primary data path is multicast — bandwidth planning must account for both.

## Key Takeaways
- Unicast: universal compatibility, linear network cost with reader count.
- Multicast: highly efficient fan-out, but depends on correctly configured network infrastructure and is often unavailable in cloud/VPN environments.
- Reliable delivery typically layers unicast repair traffic on top of either delivery mode.
- The right choice depends on reader count, network environment, and security/firewall requirements — there's no universal default beyond "check what your network actually supports."
- Always validate multicast connectivity explicitly before relying on it in cloud or containerized deployments.
