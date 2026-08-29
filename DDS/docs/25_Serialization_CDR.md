# 25. Serialization (CDR)

## Overview
Once a type is defined in IDL, DDS needs a byte-level, wire-compatible way to encode and decode it across languages, compilers, and CPU architectures. That job belongs to **CDR — Common Data Representation** — the encoding rule set inherited from CORBA and mandated by the RTPS specification for interoperability between DDS vendors.

## Core Idea
CDR defines exactly how each IDL primitive and constructed type is laid out in bytes:
- Fixed-size primitives (`long`, `double`, `boolean`, etc.) are encoded directly, honoring **alignment** rules (e.g., a `double` must start on an 8-byte boundary).
- Variable-length types (`string`, `sequence`) are prefixed with a 4-byte length field.
- Endianness is explicit and negotiated per-message: RTPS submessage headers include a flag indicating whether the payload is big-endian or little-endian CDR, so heterogeneous systems (e.g., ARM little-endian embedded device talking to a big-endian mainframe) can interoperate correctly.

## Example: Encoding Walkthrough
Given the struct:
```idl
struct Sample {
  long id;          // 4 bytes
  double value;     // 8 bytes, needs 8-byte alignment
  string<16> label;
};
```

Encoding `{ id: 7, value: 3.14, label: "ok" }` in little-endian CDR:
```
Offset 0:  07 00 00 00              -> id = 7           (4 bytes)
Offset 4:  00 00 00 00              -> padding to align value to 8-byte boundary
Offset 8:  1F 85 EB 51 B8 1E 09 40  -> value = 3.14      (8 bytes, IEEE-754)
Offset 16: 03 00 00 00              -> string length = 3 (includes null terminator)
Offset 20: 6F 6B 00                 -> "ok\0"            (3 bytes)
```
Notice the **4-byte padding** inserted before `value` — CDR alignment rules require 8-byte types to start on 8-byte boundaries, even though this "wastes" bytes. This is a classic gotcha when hand-rolling parsers or debugging with Wireshark.

## Example: XCDR1 vs XCDR2
Modern DDS implementations (RTPS 2.3+, aligned with XTypes) support two CDR variants:
- **XCDR1 (Classic CDR)**: The original encoding, always uses 4-byte length prefixes and full alignment padding — simple but can be wasteful for small fields.
- **XCDR2**: Introduced with XTypes; supports more compact encodings (`DHEADER`/`EMHEADER` for delimited/mutable types), better suited for **appendable** and **mutable** extensibility (see topic 26), at some cost in encoding complexity.

The encoding kind used is negotiated as part of type/QoS compatibility, and is visible on the wire in the RTPS submessage's representation identifier field (e.g., `CDR_LE`, `CDR2_LE`, `PL_CDR_LE` for parameter-list encoded types like discovery data).

## Example: Inspecting CDR with Wireshark
When capturing RTPS traffic, Wireshark's RTPS dissector decodes the `encapsulation kind` field (`0x0001` = `CDR_BE`, `0x0002` = `PL_CDR_BE`, etc.) and shows the parsed struct fields directly — an essential debugging technique covered further in the Monitoring/Tooling document (topic 36).

## Common Pitfalls
- Manually computing offsets without accounting for **alignment padding** — a frequent bug source when writing custom deserializers or interop shims.
- Mixing XCDR1 and XCDR2 assumptions between vendors with mismatched XTypes support, causing type "compatible on paper" but garbled on the wire.
- Assuming host-native struct layout matches CDR layout — it usually doesn't (compilers add their own padding rules that differ from CDR's).
- Ignoring endianness when writing raw sockets/interop tools outside the vendor SDK.

## Key Takeaways
- CDR is the RTPS-mandated wire encoding — it's what actually makes different vendors' DDS implementations interoperable at the byte level.
- Alignment padding (e.g., 8-byte types need 8-byte boundaries) is a common source of confusion and bugs.
- Endianness is explicit per-message, not assumed — enabling true heterogeneous-architecture interoperability.
- XCDR2 (paired with XTypes) enables more compact, evolution-friendly encodings versus classic XCDR1.
- Tools like Wireshark's RTPS dissector make CDR debugging tractable without writing a parser by hand.
