# 30. WAN and Routing (Routing/Gateway Services)

## Overview
DDS's default discovery and delivery model (multicast SPDP/SEDP, direct writer-to-reader RTPS) is fundamentally a **LAN-centric** design. Bridging DDS data across WAN links, between separate domains, through NAT boundaries, or across security enclaves requires dedicated **Routing Service** (or "Gateway") components that most major vendors provide.

## Why Not Just Run DDS Directly Over a WAN?
- Multicast typically doesn't route across WAN links (topic 22/28).
- O(n²) discovery traffic becomes untenable over high-latency, low-bandwidth links.
- NAT traversal is not natively handled by RTPS locators (which advertise potentially private/internal IPs).
- WAN links often need traffic shaping, filtering, or protocol conversion that raw peer-to-peer DDS doesn't provide.

## Routing Service: The Core Pattern
A Routing Service instance acts as a DDS participant on **each side** of a boundary, subscribing to topics on one side and re-publishing them on the other — effectively bridging two otherwise-isolated DDS "clouds" (different domains, different physical sites, or a LAN and a WAN-connected remote site).

```
[Site A: Domain 0]  <--DDS-->  [Routing Service]  <--WAN/TCP-->  [Routing Service]  <--DDS-->  [Site B: Domain 1]
```

Each Routing Service is configured with:
- **Domain routes**: which local domain(s) it participates in.
- **Session/topic routes**: which topics get bridged, with optional transformation (renaming, filtering, QoS translation) between the two sides.

## Example: Routing Service XML Config (RTI-style)
```xml
<routing_service name="SiteBridge">
  <domain_route name="Bridge">
    <participant name="SiteA">
      <domain_id>0</domain_id>
    </participant>
    <participant name="SiteB">
      <domain_id>1</domain_id>
    </participant>

    <topic_route name="VehicleStatusBridge">
      <input participant="SiteA">
        <topic_name>VehicleStatus</topic_name>
        <registered_type_name>Fleet::VehicleStatus</registered_type_name>
      </input>
      <output participant="SiteB">
        <topic_name>VehicleStatus</topic_name>
        <registered_type_name>Fleet::VehicleStatus</registered_type_name>
      </output>
    </topic_route>
  </domain_route>
</routing_service>
```

## Example: Content Filtering Across the Bridge
Routing Services commonly apply a `ContentFilteredTopic` expression on the input side to limit what actually crosses an expensive WAN link:
```xml
<input participant="SiteA">
  <topic_name>VehicleStatus</topic_name>
  <content_filter_expression>battery_level &lt; 20</content_filter_expression>
</input>
```
Here, only low-battery alerts cross the WAN, dramatically reducing bandwidth usage versus mirroring the full topic.

## NAT Traversal
Because RTPS locators embed IP:port information discovered locally, a participant behind NAT will (by default) advertise its **private** address, which is useless to a remote peer. Solutions include:
- Manually configuring **public locators** for NAT-facing participants.
- Using a Routing Service or relay positioned with a routable address on both sides, so internal participants never need to be reachable directly from the WAN.
- Vendor-specific "Cloud Discovery Service" or relay products designed specifically for NAT/WAN scenarios (e.g., RTI Cloud Discovery Service, Zenoh-based bridges for some ROS 2 deployments).

## Common Pitfalls
- Trying to run raw multicast DDS discovery directly across a WAN link — it typically doesn't route, and even if tunneled, the O(n²) discovery traffic doesn't respect WAN bandwidth/latency budgets.
- Bridging *every* topic across a WAN link by default instead of selectively routing/filtering only what's needed — wastes expensive bandwidth.
- Forgetting that a Routing Service introduces an extra network hop and re-serialization step, adding latency that must be budgeted for in real-time systems.
- Not planning for Routing Service redundancy — a single bridge instance is a single point of failure for cross-site connectivity.

## Key Takeaways
- Native DDS discovery/delivery is LAN-oriented; WAN, NAT, and cross-domain bridging require a dedicated Routing/Gateway Service layer.
- Routing Services subscribe on one side and republish on the other, optionally applying content filtering, QoS translation, or topic renaming.
- Content filtering at the bridge point is a key technique for controlling expensive WAN bandwidth usage.
- NAT traversal requires explicit public locator configuration or a relay/gateway with a routable address.
- Treat cross-site bridges as critical infrastructure requiring redundancy planning, just like any other network chokepoint.
