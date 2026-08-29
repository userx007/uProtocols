# 17. RTPS Message and Submessage Structure

## Overview

If topic 16 established *why* RTPS exists, this topic goes one level deeper into *what
actually goes over the wire*. Understanding RTPS's message/submessage structure is what
separates engineers who can competently read a Wireshark capture, diagnose a reliability
issue from a packet trace, or reason precisely about bandwidth usage, from those who
only understand DDS at the API level.

---

## The Big Picture: One UDP Packet, One RTPS Message, Many Submessages

Every RTPS **Message** is carried inside a single network packet (typically one UDP
datagram) and consists of:

```
RTPS Message
 ├── RTPS Header (fixed, 20 bytes)
 └── One or more Submessages, back to back
      ├── Submessage 1 (e.g., INFO_TS)
      ├── Submessage 2 (e.g., DATA)
      ├── Submessage 3 (e.g., HEARTBEAT)
      └── ...
```

Packing multiple submessages into one physical packet is a deliberate efficiency
design: rather than one packet per logical operation, RTPS batches related submessages
together — e.g., a single UDP packet might carry a timestamp submessage followed by
several DATA submessages for different samples, followed by a HEARTBEAT, all in one
network round-trip.

---

## RTPS Header

The fixed 20-byte header appears once per message, identifying the protocol and
sending participant:

| Field | Size | Purpose |
|---|---|---|
| `protocol` | 4 bytes | Literal `"RTPS"` — identifies this as an RTPS packet. |
| `protocolVersion` | 2 bytes | RTPS protocol version (e.g., 2.3, 2.5). |
| `vendorId` | 2 bytes | Identifies which vendor's implementation sent this (RTI, eProsima, Eclipse, etc.) — used for vendor-specific interoperability handling if needed. |
| `guidPrefix` | 12 bytes | The **GUID prefix** of the sending Participant — combined with per-entity Entity IDs in submessages to form full GUIDs (topic 19). |

Everything that follows in the packet is one or more submessages, all implicitly
associated with this sending participant's GUID prefix unless a submessage explicitly
overrides it.

---

## Submessage Structure

Each submessage has its own small header followed by submessage-specific content:

```
Submessage
 ├── submessageId    (1 byte)  — identifies the kind (DATA, HEARTBEAT, ACKNACK, GAP, INFO_TS, ...)
 ├── flags           (1 byte)  — kind-specific flags (endianness, presence of optional fields, etc.)
 ├── submessageLength (2 bytes) — length of this submessage's content
 └── submessage-specific content
```

This self-describing, length-prefixed structure is what allows a receiver to parse a
sequence of submessages of varying kinds and sizes packed into one packet, and — as
mentioned in topic 16 — is what allows the protocol to be **extended** over time: an
older implementation encountering an unrecognized submessage ID it doesn't understand
can, depending on flags, skip over it using the length field rather than failing to
parse the rest of the message.

---

## The Core Submessage Kinds

| Submessage | Purpose |
|---|---|
| **`DATA`** | Carries an actual sample's serialized payload (or key-only data for a dispose/unregister), along with the writer's GUID, the sample's sequence number, and instance-related flags. |
| **`HEARTBEAT`** | Sent periodically (or on-demand) by a reliable writer, announcing the range of sequence numbers currently available in its history — the trigger for readers to detect and request retransmission of gaps (topic 18). |
| **`ACKNACK`** | Sent by a reliable reader in response to a HEARTBEAT (or periodically), acknowledging received sequence numbers and explicitly naming any missing ones ("nacking" them) to request retransmission. |
| **`GAP`** | Sent by a writer to explicitly tell a reader "these sequence numbers will never be sent/retransmitted" — e.g., because samples were disposed, expired via Lifespan, or otherwise irrelevantly discarded — so the reader stops waiting for them. |
| **`INFO_TS`** | Carries a timestamp applied to the DATA submessage(s) that follow it — used for source-timestamp-based ordering (Destination Order QoS) and for populating `SampleInfo.source_timestamp`. |
| **`INFO_DST`** | Specifies the destination GUID prefix for subsequent submessages, when a message needs to be addressed more specifically than "whoever is listening." |
| **`INFO_REPLY`** | Provides an alternate locator (address/port) the receiver should use for replies, useful in certain NAT/multi-interface network configurations. |
| **`HEARTBEAT_FRAG`** / **`NACK_FRAG`** | Fragment-level equivalents of HEARTBEAT/ACKNACK, used when a sample is too large for one packet and must be fragmented (see below). |

---

## Example: A Typical Reliable Write, on the Wire

Walking through what actually happens over the network for a single `write()` call on a
`RELIABLE` DataWriter:

```
1. Writer's write() call is translated internally into:
   Packet 1 (writer → multicast/unicast):
     [RTPS Header (guidPrefix = Writer's Participant)]
     [INFO_TS  — source timestamp for the sample that follows]
     [DATA     — the serialized sample, sequence number N, writer GUID]

2. Periodically (or triggered by writer-side heuristics), the writer sends:
   Packet 2 (writer → readers):
     [RTPS Header]
     [HEARTBEAT — "I have sequence numbers 1 through N available"]

3. Each matched reliable reader responds:
   Packet 3 (reader → writer, unicast):
     [RTPS Header (guidPrefix = Reader's Participant)]
     [ACKNACK   — "I have received up to N, no gaps" (or: "missing sequence numbers X, Y")]

4. If gaps were nacked, the writer retransmits only those specific samples:
   Packet 4 (writer → reader, unicast):
     [RTPS Header]
     [DATA — retransmission of sequence number X]
```

This is precisely why RTPS reliability resembles TCP's sliding-window retransmission in
spirit — selective, sequence-number-driven retransmission — while remaining a
**per-sample, per-instance, connectionless** protocol layered over UDP, not a
byte-stream, connection-oriented protocol like TCP itself.

---

## Fragmentation: Handling Large Samples

UDP datagrams have practical size limits (commonly capped well below the theoretical
65,507-byte maximum, often configured around the path MTU, ~1400–9000 bytes depending on
jumbo frame support). A DDS sample larger than this limit — e.g., a large point cloud,
an image, a big configuration blob — must be **fragmented**:

- The writer splits the serialized sample into multiple `DATA_FRAG` submessages, each
  carrying one fragment plus fragmentation metadata (fragment number, total fragment
  count).
- The reader reassembles fragments before the sample is delivered to the application —
  fragment loss under `RELIABLE` reliability is handled via `NACK_FRAG` (fragment-level
  ACKNACK) rather than requiring the entire sample to be resent from scratch.

Understanding fragmentation matters operationally: very large samples increase
retransmission cost under packet loss (a single lost fragment can delay delivery of the
whole reassembled sample) and interact with network MTU configuration — a frequent
tuning consideration in high-bandwidth data topics (e.g., camera/LIDAR feeds).

---

## Practical Relevance for Engineers

- **Reading a Wireshark RTPS capture** (topic 36) means recognizing this exact
  structure: one packet, one header, a sequence of submessages — being able to spot a
  HEARTBEAT/ACKNACK exchange immediately tells you reliability is (or isn't) functioning
  as expected.
- **Diagnosing "reliable but slow" issues** often comes down to observing the
  HEARTBEAT/ACKNACK cadence and retransmission pattern directly at this level, rather
  than guessing from application-level symptoms alone.
- **Bandwidth estimation** for a given Topic and QoS configuration requires accounting
  for not just DATA submessage payload size, but also periodic HEARTBEAT/ACKNACK
  overhead — which scales with reliable writer/reader counts and configured heartbeat
  periods.
- **MTU and fragmentation tuning** matters directly for large-payload topics —
  understanding when/why fragmentation kicks in avoids surprises in production
  bandwidth or latency behavior.

---

## Summary

RTPS packs one or more self-describing, length-prefixed **submessages** (DATA,
HEARTBEAT, ACKNACK, GAP, INFO_TS, and others) behind a single fixed 20-byte header into
each network message, typically one per UDP packet — an efficient, extensible design
that allows multiple logical operations to be batched into one round-trip and allows the
protocol to evolve without breaking older parsers. A reliable write's full lifecycle on
the wire involves DATA (the sample itself), periodic HEARTBEAT announcements of
available sequence numbers, and reader-driven ACKNACK responses that trigger selective,
per-sample retransmission — closely mirroring TCP's sliding-window philosophy while
remaining a connectionless, per-sample protocol over UDP. Large samples exceeding
practical UDP/MTU limits are automatically fragmented and reassembled, with their own
fragment-level reliability submessages. Concrete familiarity with this structure is a
prerequisite for real network-level DDS troubleshooting and bandwidth reasoning.

---

## Things to Remember

- **One RTPS message = one packet (usually) = one header + multiple submessages**, batched for efficiency.
- **Core submessages**: `DATA` (the sample), `HEARTBEAT` (writer announces available sequence range), `ACKNACK` (reader acks/nacks), `GAP` (writer says "these will never be resent"), `INFO_TS` (timestamp for following DATA).
- **Reliable delivery works like TCP's sliding window in spirit**, but is connectionless, per-sample, and layered over UDP — not a byte-stream protocol.
- **Large samples are automatically fragmented** (`DATA_FRAG`) when they exceed practical UDP/MTU limits, with fragment-level reliability via `NACK_FRAG`.
- **The submessage format is self-describing and length-prefixed**, enabling protocol extensibility — unrecognized submessages can be skipped rather than breaking parsing.
- **HEARTBEAT/ACKNACK overhead is a real bandwidth cost**, scaling with reliable writer/reader counts and heartbeat period — factor this into bandwidth planning, not just raw sample payload size.
- **Concrete knowledge of this structure is required for effective Wireshark-based RTPS troubleshooting** (topic 36) — abstract "DDS uses some protocol" understanding isn't sufficient at this level.
