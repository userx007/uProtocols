# 23. IDL and Data Type Definition

## Overview
DDS is data-centric: the *type* of data flowing over a topic is a first-class contract between writers and readers. The **Interface Definition Language (IDL)**, standardized by the OMG, is the language-neutral way to define these types. Code generators then translate IDL into native language bindings (C++, Java, Python, C#, Rust, etc.).

## Core Idea
An IDL file defines `struct`s, `enum`s, `union`s, `typedef`s, and modules (namespaces) that describe the shape of the data exchanged. This same definition is used to:
1. Generate strongly-typed language bindings for reading/writing samples.
2. Derive the CDR serialization layout (see topic 25).
3. Compute **type compatibility** for XTypes evolution (see topic 26).

## Example: A Simple IDL Struct
```idl
module Fleet {

  enum VehicleStatusCode {
    IDLE,
    ACTIVE,
    FAULT
  };

  @topic
  struct VehicleStatus {
    @key
    long vehicle_id;
    VehicleStatusCode status;
    double battery_level;
    string<64> location_name;
    sequence<double, 3> position;   // e.g., x, y, z
  };
}
```

Key annotations:
- `@key` marks the field(s) that form the **instance key** (see topic 24) — DDS uses this to distinguish separate "instances" published on the same topic.
- `@topic` (vendor-specific / newer IDL versions) marks a struct as directly usable as a DDS Topic type.
- `string<64>` and `sequence<double, 3>` are **bounded** types — bounding is important for both wire efficiency and for satisfying safety-critical/embedded memory constraints.

## Example: Union and Nested Types
```idl
union Command switch (long) {
  case 1: string move_command;
  case 2: double speed_command;
  case 3: boolean stop_command;
};

struct Waypoint {
  double x;
  double y;
};

struct RoutePlan {
  @key long plan_id;
  sequence<Waypoint> waypoints;
  Command next_action;
};
```
Unions let a single field carry one of several possible types, discriminated by a switch value — useful for command/control topics with variant payloads.

## Example: Code Generation Workflow
```bash
# RTI Connext
rtiddsgen -language C++11 VehicleStatus.idl

# Eclipse Cyclone DDS
idlc -l cxx VehicleStatus.idl

# eProsima Fast DDS
fastddsgen VehicleStatus.idl
```
Each generates: the native type class(es), a `TypeSupport` class for registering the type with the middleware, and (de)serialization code implementing CDR.

## Common Pitfalls
- Using **unbounded** `string`/`sequence` types in resource-constrained or real-time systems — unbounded types force dynamic memory allocation on receive, which is bad for determinism.
- Forgetting `@key` on the field(s) meant to distinguish instances — without it, all samples land in a single unkeyed instance, defeating features like Ownership and per-instance history.
- Regenerating code from a changed IDL without considering **type compatibility** rules (topic 26) — this can silently break interoperability with already-deployed writers/readers using the old type.
- Mismatched IDL between vendors' generators for identical semantics (e.g., default bounds) causing subtle wire incompatibilities — always verify with the target middleware's IDL compiler.

## Key Takeaways
- IDL is the vendor-neutral **source of truth** for a topic's data type, from which native bindings and wire serialization are both derived.
- `@key` fields define instance identity — critical to understand before using Ownership, Durability, or instance-based history.
- Prefer **bounded** strings/sequences for deterministic, real-time-friendly systems.
- The IDL compiler (`rtiddsgen`, `idlc`, `fastddsgen`, etc.) is the bridge between the abstract type and concrete generated code.
- Changing IDL after deployment is a type-evolution problem — see XTypes (topic 26) before doing so carelessly.
