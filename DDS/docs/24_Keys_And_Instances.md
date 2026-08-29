# 24. Keys and Instances

## Overview
DDS topics aren't just simple streams of anonymous messages — they can represent **collections of independently-tracked objects** ("instances") multiplexed onto a single topic. This is one of DDS's most distinctive features compared to typical message brokers, and it hinges entirely on the concept of **keyed data**.

## Core Idea
A **key** is one or more fields in a topic's data type (marked `@key` in IDL) whose combined value identifies a distinct **instance**. All samples published with the same key value belong to the same instance's timeline; samples with different key values are entirely independent streams that happen to share a topic/type.

Think of a topic like `VehicleStatus` keyed by `vehicle_id`: publishing status for vehicles 101, 102, and 103 on the *same* topic creates three separate instances, each with its own history, ownership, and lifecycle — even though it's "one topic" from a wiring/QoS perspective.

## Instance States
Every instance tracked by a DataReader has one of three states:

| State | Meaning |
|---|---|
| `ALIVE` | At least one matched writer is actively writing this instance. |
| `NOT_ALIVE_DISPOSED` | A writer explicitly called `dispose()` on this instance — it's logically "deleted." |
| `NOT_ALIVE_NO_WRITERS` | No writer is currently writing this instance (all writers left/died) but it wasn't explicitly disposed. |

## Example: Instance Lifecycle in Code (pseudocode, C++-style API)
```cpp
VehicleStatus v1;
v1.vehicle_id = 101;
v1.status = ACTIVE;

// register_instance is optional but pre-computes the instance handle for efficiency
InstanceHandle_t handle = writer->register_instance(v1);

v1.status = ACTIVE;
writer->write(v1, handle);      // sample 1 for instance 101

v1.battery_level = 42.0;
writer->write(v1, handle);      // sample 2, same instance 101

// Vehicle 101 goes offline / is retired:
writer->dispose(v1, handle);    // instance -> NOT_ALIVE_DISPOSED for all readers
```

Meanwhile, a reader can query per-instance:
```cpp
LoanedSamples<VehicleStatus> samples = reader->take_instance(handle_for_101);
for (auto& s : samples) {
  if (s.info().valid_data) {
    // process latest data for vehicle 101
  } else if (s.info().instance_state() == NOT_ALIVE_DISPOSED) {
    // vehicle 101 was explicitly retired
  }
}
```

## Example: Instance Handles
An `InstanceHandle_t` is a cached, opaque, efficient reference to a key value — computed once via `register_instance()` so that subsequent `write()`/`read()`/`take()` calls don't need to re-hash/re-compare the full key fields every time. This matters for performance in high-instance-count systems (e.g., thousands of IoT sensors on one topic).

## Interaction With Other QoS
- **History QoS** (topic 9) is applied *per instance*, not globally — `KEEP_LAST(3)` means the last 3 samples *per instance*, not the last 3 samples across the whole topic.
- **Ownership QoS** (topic 12) with `EXCLUSIVE` mode is arbitrated *per instance* — different instances can have different "owning" writers.
- **Resource Limits** QoS can bound `max_instances`, `max_samples_per_instance`, and `max_samples` independently.

## Common Pitfalls
- Forgetting that unkeyed topics (no `@key` fields) have exactly **one implicit instance** — all samples share the same instance timeline, which surprises people expecting per-sample independence.
- Not calling `dispose()` when logically removing an object — leaves a `NOT_ALIVE_NO_WRITERS` instance lingering in readers' caches indefinitely (or until resource limits evict it).
- Using large numbers of instances without bounding `max_instances`, risking unbounded memory growth on the reader side.

## Key Takeaways
- Keys turn one topic into a multiplexed collection of independent instances — this is central to DDS's data-centric model.
- Every instance has a state: `ALIVE`, `NOT_ALIVE_DISPOSED`, or `NOT_ALIVE_NO_WRITERS`.
- `register_instance()` yields a fast, reusable `InstanceHandle_t` — worth using in high-throughput, high-instance-count scenarios.
- History, Ownership, and Resource Limits QoS all operate at the **instance** granularity, not the topic granularity.
- Always `dispose()` instances that are logically going away, to avoid stale state lingering in readers.
