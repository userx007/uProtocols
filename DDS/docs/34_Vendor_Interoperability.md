# 34. Vendor Interoperability

## Overview
One of DDS's core value propositions is that RTPS is a standardized wire protocol — meaning DataWriters from one vendor's implementation should interoperate with DataReaders from another's. In practice, this works well for the common cases but has real, well-known edge cases worth understanding. The major implementations are **RTI Connext DDS**, **eProsima Fast DDS**, **Eclipse Cyclone DDS**, and **OpenDDS**.

## Why Interoperability Mostly Works
The OMG **RTPS specification** standardizes:
- Wire message format (headers, submessages)
- CDR serialization rules
- Discovery protocol behavior (SPDP/SEDP)
- QoS semantics and compatibility matching rules

Vendors that pass the **OMG RTPS interoperability demonstrations/testing** can be expected to discover and exchange data with each other for standard use cases — this has been demonstrated at OMG interoperability events for years across all major implementations.

## Common Interoperability Pitfalls

### 1. Vendor-Specific QoS Extensions
Each vendor offers extensions beyond the standard QoS set (e.g., RTI's `TRANSPORT_SELECTION` QoS, Cyclone's specific `ignorelocal` behavior). These extensions are invisible/ignored by other vendors' implementations — usually harmless, but can create subtle behavioral differences at the edges.

### 2. XTypes Support Maturity
Not all vendors implement the full **XTypes** (topic 26) specification, or implement different subsets/versions of it. A `MUTABLE` type using advanced `TypeObject` exchange might work perfectly between two RTI Connext participants but fail to match against an older Cyclone DDS version with partial XTypes support — falling back to name-only or basic type checking, or failing to match at all.

### 3. Discovery Timing and Defaults
Default lease durations, announcement periods, and initial-peer behaviors differ slightly across vendors. Mixed-vendor systems sometimes see asymmetric discovery timing (Vendor A discovers Vendor B quickly, but B takes longer to discover A) due to differing default retry/backoff behavior.

### 4. Built-in Topic QoS Defaults
Vendors sometimes ship with different default QoS values for user-created topics (e.g., default `history` depth, default `reliability`). Since QoS compatibility is a *request vs. offered* check (topic 15), a mismatch here between vendors' defaults is a common source of "why aren't my endpoints matching" bugs when the application code doesn't explicitly set every relevant QoS policy.

### 5. Security Plugin Interop
DDS Security's reference plugins are standardized, but not every vendor bundles them identically, and custom/proprietary security plugins are (by design) not required to interoperate with other vendors' custom plugins — only the standard "builtin" plugin set is interoperability-tested.

## Example: A Real Debugging Scenario
```
Symptom: RTI Connext writer and Cyclone DDS reader discover each other
         (visible in each side's participant list) but samples never arrive.

Investigation:
  1. Confirm topic name and registered type name match exactly (case-sensitive).
  2. Compare QoS profiles side-by-side — found: Connext writer had
     RELIABLE + TRANSIENT_LOCAL durability; Cyclone reader defaulted to
     VOLATILE durability with no explicit override in its QoS profile.
  3. Per QoS compatibility rules (topic 15), TRANSIENT_LOCAL offered vs.
     VOLATILE requested IS compatible (offered durability >= requested is fine)...
     but on closer inspection, the actual issue was BEST_EFFORT vs RELIABLE mismatch
     in the reverse direction, which is NOT compatible.
  4. Fix: explicitly set matching Reliability QoS on both sides rather than
     relying on each vendor's differing defaults.
```
This illustrates the most common real-world interop bug pattern: **implicit vendor-default QoS mismatches**, not fundamental wire-protocol incompatibility.

## Best Practices for Multi-Vendor Systems
- Always **explicitly set QoS** relevant to matching (reliability, durability, history) rather than relying on vendor defaults.
- Use `FINAL` or `APPENDABLE` (not `MUTABLE`) types when mixing vendors with uncertain/partial XTypes support, for the broadest compatibility.
- Test discovery and matching explicitly in a mixed-vendor lab environment before assuming production interoperability.
- Use vendor-neutral tools (Wireshark's RTPS dissector, `rtiddsspy`-equivalent generic spy tools) to verify wire-level compatibility independent of any single vendor's diagnostic tooling.
- Track which OMG interoperability test suite version each vendor's release has passed, especially for newer features like XTypes 1.3 and DDS Security.

## Key Takeaways
- RTPS standardization means cross-vendor interoperability generally works for core pub/sub — this has been proven at OMG interop events for years.
- The most common real-world interop failures come from **implicit QoS default mismatches**, not fundamental protocol incompatibility.
- XTypes and DDS Security feature support maturity varies by vendor and version — verify before relying on advanced features across vendors.
- Explicitly specifying QoS (rather than relying on defaults) is the single highest-leverage practice for reliable multi-vendor interoperability.
- When in doubt, verify at the wire level (Wireshark RTPS dissector) rather than trusting only vendor-specific diagnostic tools.
