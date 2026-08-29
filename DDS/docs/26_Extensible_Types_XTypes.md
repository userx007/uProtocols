# 26. Extensible Types (XTypes)

## Overview
Real systems evolve — new fields get added, old ones deprecated — but DDS deployments often can't be upgraded all at once (think: a fleet of vehicles, or a multi-year defense program). **XTypes** is the OMG standard that lets writers and readers using *different but compatible versions* of a type still interoperate, instead of requiring a fully synchronized "big bang" upgrade.

## Core Idea: Extensibility Kinds
Every IDL type has an extensibility kind, which governs what kinds of changes are allowed while remaining wire-compatible:

| Kind | IDL Annotation | Allows |
|---|---|---|
| `FINAL` | `@final` | No changes at all — strictest, most compact encoding. |
| `APPENDABLE` | `@appendable` (often the default) | New fields may be **appended** at the end; existing fields can't be reordered or removed. |
| `MUTABLE` | `@mutable` | Fields can be added, removed, or reordered; each field is tagged with an explicit ID on the wire. |

## Example: Appendable Evolution
```idl
// v1 — deployed on legacy vehicles
@appendable
struct VehicleStatus {
  @key long vehicle_id;
  double battery_level;
};

// v2 — deployed on newer vehicles, adds a field
@appendable
struct VehicleStatus {
  @key long vehicle_id;
  double battery_level;
  double tire_pressure;   // NEW - appended at the end
};
```
A v1 reader receiving a v2 sample simply ignores `tire_pressure` (it wasn't expecting it). A v2 reader receiving a v1 sample gets a default/absent value for `tire_pressure`. Both directions work **without any redeployment**, as long as the new field was appended, not inserted.

## Example: Mutable Evolution with Field IDs
```idl
@mutable
struct Command {
  @id(1) long command_id;
  @id(2) string action;
  @id(3) @optional double timeout_sec;   // added later, marked optional
};
```
Because each field carries an explicit `@id`, mutable types tolerate **reordering** and **removal** as well as addition — at the cost of a heavier per-field wire header (`EMHEADER` in XCDR2) versus the leaner appendable/final encodings.

## Type Compatibility Checking (TypeObject / TypeConsistencyEnforcement)
During SEDP (topic 21), participants can exchange a **TypeObject** — a full structural description of the type — instead of relying solely on a type name match. The middleware then runs **type consistency checking** using rules like:
- Are extensibility kinds compatible?
- Do all common fields have matching types?
- Are any newly required (non-optional) fields on one side unmatched on the other?

QoS policy `TypeConsistencyEnforcementQosPolicy` lets you control strictness — e.g., `DISALLOW_TYPE_COERCION` for a strict match, or allowing coercion for looser interoperability.

## Example: Optional Fields
```idl
struct Telemetry {
  @key long sensor_id;
  double reading;
  @optional string units;   // may be absent entirely on the wire
};
```
`@optional` fields are explicitly represented as present/absent, letting a schema add fields without forcing every writer to populate them.

## Common Pitfalls
- Assuming `FINAL` types can evolve — they can't; any field change breaks wire compatibility and requires full redeployment.
- Inserting or reordering fields in an `APPENDABLE` type — only append-at-the-end changes are safe; reordering silently breaks compatibility.
- Ignoring `TypeConsistencyEnforcement` QoS settings and being surprised when "compatible-looking" types still refuse to match.
- Not testing both directions (old reader/new writer AND new reader/old writer) — evolution bugs are often asymmetric.

## Key Takeaways
- XTypes lets systems evolve data types over time without forcing synchronized upgrades — critical for long-lived, incrementally-deployed systems.
- Three extensibility kinds — `FINAL`, `APPENDABLE`, `MUTABLE` — trade off flexibility against wire compactness and complexity.
- `APPENDABLE` is the common sweet spot: cheap like `FINAL`, but tolerant of field additions.
- `MUTABLE` (with explicit `@id` per field) is the most flexible but has more wire overhead.
- Always test compatibility in **both** old↔new directions before rolling out a type change to a live system.
