# 35. Content-Filtered and Multi-Topics

## Overview
Beyond the plain `Topic`, DDS defines two specialized topic types that let readers reduce or reshape the data they receive **before it ever leaves the writer's side of the network** (for content filters propagated to the writer) or **combine multiple topics into one logical view**. These are `ContentFilteredTopic` and `MultiTopic`.

## ContentFilteredTopic
A `ContentFilteredTopic` wraps a base topic with an SQL-like filter expression. A DataReader created on it only receives samples matching the filter — and critically, well-behaved DDS implementations propagate this filter to the **writer side** during discovery, so non-matching samples aren't even sent, saving bandwidth.

### Example: Basic Filter
```cpp
Topic<VehicleStatus> baseTopic(participant, "VehicleStatus");

ContentFilteredTopic<VehicleStatus> lowBatteryTopic(
    baseTopic,
    "LowBatteryVehicles",
    Filter("battery_level < %0 AND status = %1", {"20", "'ACTIVE'"})
);

DataReader<VehicleStatus> reader(subscriber, lowBatteryTopic);
```
Only samples where `battery_level < 20 AND status = 'ACTIVE'` are delivered to this reader — a vehicle with `battery_level = 80` never crosses the wire to this particular reader (assuming writer-side filtering is supported and negotiated, as it is by most major vendors).

### Filter Expression Syntax
DDS filter expressions resemble SQL `WHERE` clauses:
```sql
battery_level < 20 AND status = 'ACTIVE'
vehicle_id BETWEEN 100 AND 200
location_name LIKE 'Warehouse%'
```
Parameters (`%0`, `%1`, ...) can be updated at runtime without recreating the `ContentFilteredTopic`:
```cpp
lowBatteryTopic.set_expression_parameters({"15", "'ACTIVE'"});
```

### Why Writer-Side Filtering Matters
```
Without CFT:  Writer sends ALL samples --> Network --> Reader discards unwanted ones locally
With CFT:     Writer evaluates filter locally --> only matching samples cross the network
```
For high-frequency topics with many uninterested readers (e.g., a fleet of 10,000 vehicles but a reader only interested in 5 of them), this difference is enormous for bandwidth and CPU usage.

## MultiTopic
A `MultiTopic` lets a reader subscribe to a **derived, computed view** that joins/aggregates data across multiple related topics — conceptually similar to a SQL `JOIN` or `SELECT` projection, computed by the DDS middleware itself.

### Example
```cpp
// Assume two topics: "VehicleLocation" (vehicle_id, x, y)
//                 and "VehicleStatus"   (vehicle_id, battery_level, status)

MultiTopic<VehicleSummary> summaryTopic(
    participant,
    "VehicleSummary",
    "SELECT vehicle_id, x, y, battery_level FROM VehicleLocation, VehicleStatus WHERE VehicleLocation.vehicle_id = VehicleStatus.vehicle_id"
);

DataReader<VehicleSummary> reader(subscriber, summaryTopic);
```
The reader sees a single combined `VehicleSummary` sample whenever either constituent topic updates for a given `vehicle_id`, without the application needing to manually correlate two separate DataReaders itself.

**Note**: `MultiTopic` support and exact semantics vary meaningfully by vendor — it's a less universally implemented feature than `ContentFilteredTopic`, and some vendors deprecate or only partially support it in favor of application-level correlation. Always verify vendor support before designing around it.

## Comparison

| Feature | Purpose | Writer-Side Optimization | Vendor Support |
|---|---|---|---|
| `ContentFilteredTopic` | Filter rows (samples) by predicate | Yes (widely supported) | Broad, mature |
| `MultiTopic` | Join/project across multiple topics | N/A (reader-side composition) | Partial/varies |

## Common Pitfalls
- Writing complex filter expressions that some vendors' filter engines don't support (e.g., certain string functions) — always check the vendor's supported expression grammar subset.
- Assuming filter parameter updates are free — updating parameters can trigger SEDP re-announcement/re-matching in some implementations, adding overhead if done extremely frequently.
- Relying on `MultiTopic` for critical application logic without verifying the target vendor's support level and exact join semantics, given its lower/varying adoption.
- Forgetting that `ContentFilteredTopic` filtering happens on **already-serialized-and-then-deserialized-for-evaluation** data on the writer side — extremely complex filters can add non-trivial CPU cost on high-frequency writers with many distinct reader filters to evaluate.

## Key Takeaways
- `ContentFilteredTopic` lets readers request only samples matching an SQL-like predicate, with writer-side filtering (in most implementations) saving real network bandwidth.
- `MultiTopic` provides a middleware-level join/projection across related topics, but has less consistent vendor support than `ContentFilteredTopic`.
- Filter parameters can be updated at runtime without recreating the topic, enabling dynamic interest changes.
- Always validate a given vendor's supported filter expression grammar and `MultiTopic` semantics before architecting around them.
- These features are key tools for reducing bandwidth and application-level correlation complexity in large, high-fan-out systems.
