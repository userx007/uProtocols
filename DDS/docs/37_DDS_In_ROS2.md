# 37. DDS in ROS 2

## Overview
ROS 2 (Robot Operating System 2) replaced ROS 1's custom TCP-based transport with **DDS** as its default middleware, gaining decentralized discovery, built-in QoS control, and multi-vendor interoperability out of the box. Understanding how ROS 2 concepts map onto DDS primitives is essential for any engineer debugging or tuning ROS 2 systems at the middleware level.

## The rmw Abstraction Layer
ROS 2 doesn't talk to a specific DDS vendor directly — it uses the **ROS Middleware (rmw)** abstraction interface, with a swappable implementation per vendor:

| rmw Implementation | Underlying DDS |
|---|---|
| `rmw_fastrtps_cpp` | eProsima Fast DDS (ROS 2 default for many distros) |
| `rmw_cyclonedds_cpp` | Eclipse Cyclone DDS |
| `rmw_connextdds` | RTI Connext DDS |
| `rmw_gurumdds_cpp` | GurumNetworks GurumDDS |

Switching is typically just an environment variable:
```bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ros2 run my_package my_node
```
This lets teams choose based on licensing, embedded footprint, security certification, or performance characteristics without changing application code.

## ROS 2 Concepts → DDS Concepts

| ROS 2 Concept | DDS Equivalent |
|---|---|
| Node | `DomainParticipant` (roughly — some rmw layers use one participant per node, others share) |
| Topic | DDS `Topic` |
| Publisher/Subscription | `DataWriter` / `DataReader` |
| Message (`.msg` file) | IDL-generated type (ROS 2 message definitions compile to DDS-compatible IDL under the hood) |
| Service | Two DDS topics (request + reply) under the hood, correlated by a GUID |
| ROS Domain ID (`ROS_DOMAIN_ID`) | DDS Domain ID directly |

## Example: ROS 2 QoS Profiles Map Directly to DDS QoS
```python
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy

qos = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
    depth=10
)

publisher = node.create_publisher(SensorMsg, 'sensor_data', qos)
```
This is a thin wrapper directly setting the underlying DDS `ReliabilityQosPolicy`, `DurabilityQosPolicy`, and `HistoryQosPolicy` (topics 7, 8, 9) — a `TRANSIENT_LOCAL` publisher, for instance, will deliver its last samples to late-joining subscribers exactly as raw DDS would.

### Common ROS 2 QoS Presets
- **Sensor Data profile**: `BEST_EFFORT` + `VOLATILE` — optimized for high-rate, loss-tolerant streams like camera/lidar data.
- **Default profile**: `RELIABLE` + `VOLATILE` — general-purpose command/state topics.
- **Services**: Always `RELIABLE` (a lost request or reply would break the request/response semantic entirely).

**QoS mismatch in ROS 2 manifests exactly like raw DDS**: a publisher on `BEST_EFFORT` and a subscriber requesting `RELIABLE` will discover each other but silently fail to match — a very common ROS 2 gotcha, especially when mixing a sensor driver's `BEST_EFFORT` publisher with a naively-default `RELIABLE` subscriber.

## Example: Domain Bridging Between Robots
Multiple robots on the same network but needing isolation typically use different `ROS_DOMAIN_ID` values (mapping directly to DDS Domain ID isolation, topic 2):
```bash
# Robot 1
export ROS_DOMAIN_ID=1
# Robot 2
export ROS_DOMAIN_ID=2
```
Cross-robot communication then requires an explicit DDS Routing Service bridge (topic 30) or ROS 2's own `domain_bridge` package, rather than accidental cross-talk — a deliberate safety feature for multi-robot fleets.

## Discovery Tuning in ROS 2
Because ROS 2 nodes often run in Docker/Kubernetes where multicast is unreliable, `SIMPLE` discovery (raw SPDP/SEDP) is sometimes swapped for **Fast DDS Discovery Server** or **Cyclone DDS's** own tuning options — directly applying the discovery scalability techniques from topic 22 to large robot fleets or multi-container deployments.

## Common Pitfalls
- Mixing `BEST_EFFORT` sensor publishers with `RELIABLE`-default subscribers, causing silent non-matching — always check QoS compatibility explicitly, especially with third-party driver packages.
- Assuming all `ROS_DOMAIN_ID` values are safely isolated on a shared network without verifying multicast/network segmentation actually enforces it.
- Not accounting for rmw-implementation-specific default QoS or performance differences when switching vendors mid-project.
- Running many ROS 2 nodes without discovery server tuning at fleet scale, hitting the same O(n²) discovery wall discussed in topic 22.

## Key Takeaways
- ROS 2 uses DDS as its default middleware via the swappable `rmw` abstraction layer, supporting multiple vendor backends.
- ROS 2 QoS profiles are thin wrappers directly over DDS QoS policies — the same compatibility (request vs. offered) rules apply.
- `ROS_DOMAIN_ID` maps directly onto DDS Domain ID for network-level isolation between robot fleets.
- QoS mismatches (especially `BEST_EFFORT` vs `RELIABLE`) are a very common real-world ROS 2 debugging scenario, directly traceable to core DDS QoS compatibility rules.
- At fleet scale, ROS 2 systems benefit from the same discovery scalability techniques (discovery servers, static peers) used in general DDS deployments.
