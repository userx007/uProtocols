# 19. Entity IDs and GUIDs

## Overview

Every RTPS Participant, Writer, and Reader referenced throughout topics 16–18 needs a
**globally unique identifier** that appears on the wire — not a language-level object
pointer or a DDS-API handle, but an actual bytes-on-the-wire identity that any receiving
implementation, regardless of vendor or language, can parse and compare. That identifier
is the **GUID**. Understanding its structure is essential for reading RTPS traces,
reasoning about discovery, and understanding how DDS achieves global uniqueness without
any central coordinating authority.

---

## GUID Structure: Prefix + Entity ID

A GUID is a fixed **16-byte** value composed of two parts:

```
GUID (16 bytes) = GUID Prefix (12 bytes) + Entity ID (4 bytes)
```

```
┌─────────────────────────────────────────┬─────────────────┐
│         GUID Prefix (12 bytes)           │  Entity ID (4B) │
│    identifies the DomainParticipant      │  identifies the │
│                                           │  entity within  │
│                                           │  that participant│
└─────────────────────────────────────────┴─────────────────┘
```

### GUID Prefix (12 bytes)

Identifies the **DomainParticipant** — every RTPS entity belonging to the same
participant shares the same 12-byte prefix. Vendors typically derive this from a
combination of values intended to make it unique without coordination:

- Parts of the host's IP address or a vendor-specific host identifier.
- The process ID of the running application.
- A per-participant counter/random component, to distinguish multiple participants in
  the same process.

The exact algorithm is vendor-specific (RTPS standardizes the *format*, not the
*generation algorithm*), but the practical effect is: **two participants on different
hosts, or in different processes on the same host, will essentially never collide** —
similar in spirit to how UUIDs achieve practical global uniqueness without central
coordination.

### Entity ID (4 bytes)

Identifies a **specific entity** (a Writer, a Reader, or the Participant itself) within
that GUID prefix's scope. The Entity ID has a structured format:

```
Entity ID (4 bytes) = Entity Key (3 bytes) + Entity Kind (1 byte)
```

- **Entity Kind** identifies what type of entity this is — a built-in discovery Writer,
  a built-in discovery Reader, a user-defined Writer, a user-defined Reader, the
  Participant itself, etc. — via standardized well-known values.
- **Entity Key** is typically a counter or hash uniquely distinguishing entities of the
  same kind within one participant (e.g., the 2nd, 3rd, 4th DataWriter created).

### Well-Known Entity IDs

Certain Entity IDs are **standardized, fixed values** — not vendor- or
instance-specific — because they identify the **built-in discovery entities** every
RTPS implementation must have, so that discovery itself can bootstrap without prior
knowledge:

| Well-known entity | Purpose |
|---|---|
| `ENTITYID_PARTICIPANT` | Identifies the Participant entity itself. |
| `ENTITYID_SEDP_BUILTIN_PUBLICATIONS_WRITER/READER` | The built-in Writer/Reader pair used for SEDP (topic 21) — announcing/discovering DataWriters. |
| `ENTITYID_SEDP_BUILTIN_SUBSCRIPTIONS_WRITER/READER` | The built-in Writer/Reader pair for announcing/discovering DataReaders. |
| `ENTITYID_SPDP_BUILTIN_PARTICIPANT_WRITER/READER` | The built-in Writer/Reader pair for SPDP (topic 20) — participant-level discovery. |

Because these values are **fixed by the RTPS specification**, any two RTPS
implementations — regardless of vendor — know exactly which Entity ID to expect for
"the thing that announces new DataWriters," etc., without any prior negotiation. This is
precisely what allows discovery to bootstrap cold: a brand-new participant can send an
SPDP announcement and know, by specification, exactly what Entity ID a receiving
participant's SPDP reader will be listening on.

---

## Why This Design Achieves Global Uniqueness Without Coordination

This is worth appreciating as a piece of distributed-systems design: DDS has **no
central registry, no naming service, no broker** assigning identities — yet GUIDs must
still be globally unique across an entire domain (and in practice, across the internet,
since RTPS doesn't assume domain-level network isolation). The GUID prefix's
derivation from host/process-specific values, combined with the Entity ID's
per-participant scoping, achieves this the same way UUIDs or IP:port combinations do:
by combining enough independently-varying, practically-non-colliding components that
collision probability becomes negligible, without requiring any participant to ask
permission or check with a central authority before creating an entity.

---

## GUIDs in Practice: What You'll See

### In built-in topic data (topic 03)

Reading the `DCPSPublication`/`DCPSSubscription` built-in topics, each discovered
DataWriter/DataReader's identity is exposed as a GUID — useful for building
introspection/monitoring tooling that needs to uniquely track specific writers/readers
across the domain, independent of any particular language-level handle.

```cpp
DataReader* builtinPubReader = participant->get_builtin_subscriber()
    ->lookup_datareader("DCPSPublication");
// Each returned PublicationBuiltinTopicData includes a BuiltinTopicKey_t
// derived from the discovered DataWriter's GUID.
```

### In a Wireshark RTPS capture

Every submessage in an RTPS packet is scoped to a `guidPrefix` from the packet header
(topic 17), combined with per-submessage Entity IDs where relevant (e.g., a DATA
submessage's `writerId` field). Recognizing well-known Entity IDs (e.g., spotting SPDP
or SEDP traffic by their fixed Entity ID values) versus user-defined ones is a practical
skill for distinguishing discovery traffic from application data traffic in a capture.

### In `InstanceHandle_t` — a related but distinct concept

It's worth explicitly distinguishing DDS's `InstanceHandle_t` (used at the DDS API
level to identify a *keyed instance* within a Topic, topic 24) from a RTPS **GUID**
(used at the wire-protocol level to identify a *Writer, Reader, or Participant entity*).
These are conceptually different axes — one identifies "which instance of data" within
a topic, the other identifies "which participant/writer/reader" on the network — though
implementations sometimes derive/relate them internally in vendor-specific ways.

---

## Practical Relevance for Engineers

- **Reading RTPS captures competently requires recognizing GUID structure** — being
  able to mentally split a 16-byte GUID into "which participant" (prefix) and "which
  entity" (Entity ID, including recognizing well-known discovery Entity IDs) is a core
  diagnostic skill.
- **GUID stability across restarts is generally not guaranteed** — since prefixes are
  commonly derived in part from process ID and similar dynamic values, a restarted
  application will typically get a **new** GUID, which matters if you're building
  tooling that tries to track "the same logical writer" across restarts by GUID alone
  (you generally shouldn't — use application-level identity/instance keys for that
  instead).
- **Multi-homed hosts and NAT'd environments** can complicate GUID-prefix-derivation
  assumptions in some vendor implementations — worth investigating vendor-specific
  behavior if deploying across complex network topologies.

---

## Summary

The GUID is RTPS's fundamental, wire-level, globally-unique identifier for
Participants, Writers, and Readers, composed of a 12-byte GUID Prefix (identifying the
DomainParticipant, derived from host/process-specific values to achieve practical
uniqueness without central coordination) and a 4-byte Entity ID (identifying the
specific entity within that participant, itself split into an Entity Kind and Entity
Key). A set of standardized, fixed **well-known Entity IDs** for built-in discovery
Writers/Readers is what allows RTPS discovery to bootstrap cold between any two
conformant implementations without prior negotiation. GUIDs are conceptually distinct
from DDS's `InstanceHandle_t` (which identifies keyed data instances, not network
entities) and are generally not stable across application restarts — a detail worth
remembering when building GUID-based tooling or diagnostics.

---

## Things to Remember

- **GUID = 12-byte GUID Prefix (identifies the Participant) + 4-byte Entity ID (identifies the specific entity within it).**
- **GUID uniqueness is achieved without any central registry** — via host/process-derived prefix components, the same design philosophy as UUIDs.
- **Well-known, fixed Entity IDs for built-in discovery entities** (SPDP/SEDP writers/readers) are what let RTPS discovery bootstrap cold between any two conformant vendors.
- **GUIDs are generally not stable across application restarts** — don't build long-term identity tracking on raw GUIDs alone; use application-level keys instead.
- **GUID ≠ InstanceHandle_t** — GUID identifies a network entity (Participant/Writer/Reader); InstanceHandle_t identifies a keyed data instance within a Topic. Different axes, don't conflate them.
- **Recognizing GUID structure and well-known Entity IDs is a core skill for reading RTPS/Wireshark captures** — it's how you distinguish discovery traffic from application data and identify which participant/entity a given submessage belongs to.
