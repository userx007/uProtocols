# 16. RTPS Protocol Overview

## Overview

Everything discussed so far — Domains, Topics, DataWriters/DataReaders, QoS — describes
DDS's **API-level abstraction**. But for two DDS applications, potentially built with
**different vendor implementations**, to actually talk to each other over a network,
there must be a standardized **wire protocol** underneath. That protocol is **RTPS
(Real-Time Publish-Subscribe Protocol)** — an OMG standard (formally, "DDS
Interoperability Wire Protocol," DDSI-RTPS) that every conformant DDS vendor implements,
guaranteeing cross-vendor interoperability.

This is a critical distinction for a senior engineer to internalize: **DDS is the API/
programming model; RTPS is the wire protocol.** You could, in principle, implement the
DDS API without RTPS (non-interoperable, proprietary wire format) — some early or niche
implementations did — but virtually every serious deployment today relies on RTPS
specifically *because* it enables mixing vendors (e.g., RTI Connext talking to Eclipse
Cyclone DDS talking to eProsima Fast DDS in the same system).

---

## Why RTPS Exists: The Interoperability Problem

Before DDSI-RTPS was standardized (2008, as part of DDS 1.2's interoperability annex,
later split into its own spec), each DDS vendor had its own proprietary wire format.
Two applications built on different vendors' DDS implementations simply could not talk
to each other, defeating much of the point of a *standard* — you'd be locked into a
single vendor for your entire distributed system.

RTPS solves this by standardizing:

- The **packet/message format** on the wire (headers, submessages).
- The **discovery protocol** (how participants and endpoints find each other —
  topics 20–21).
- The **reliability protocol** (how HEARTBEAT/ACKNACK-based retransmission works —
  topic 18).
- The **data representation** (CDR serialization — topic 25).

As a direct consequence, a senior engineer evaluating or architecting a DDS-based system
should generally treat **vendor choice as a genuinely revisable decision** — assuming
RTPS conformance and avoiding vendor-proprietary QoS extensions, you retain the ability
to mix vendors or migrate later without re-architecting the data model.

---

## RTPS Design Goals

RTPS was designed from the ground up for the performance and resilience characteristics
DDS promises:

1. **Best-effort, unreliable transports as the baseline** — RTPS is designed to run
   natively over **UDP/IP** (both unicast and multicast), *adding* reliability at the
   protocol level rather than depending on a reliable transport like TCP underneath.
   This is a deliberate choice: UDP has lower latency and overhead, no connection setup,
   and native multicast support — properties essential for real-time, many-to-many
   pub-sub. (RTPS can also run over TCP or other transports where UDP/multicast isn't
   viable, e.g., across certain WAN links — see topic 27.)
2. **No broker in the data path** — RTPS is inherently peer-to-peer; every participant
   communicates directly (unicast or multicast) with every other matched participant,
   with no intermediary process relaying data.
3. **Best-effort AND reliable delivery, both natively supported** — unlike TCP (always
   reliable) or raw UDP (always best-effort), RTPS lets each data flow choose,
   implementing reliability as an *optional layer* on top of an inherently best-effort
   transport (topic 18's HEARTBEAT/ACKNACK mechanism).
4. **Extensibility** — the protocol's submessage-based structure (topic 17) allows new
   submessage types and parameters to be added over time without breaking older
   implementations, which has allowed RTPS to evolve (e.g., to support DDS Security,
   XTypes) while remaining backward compatible.

---

## The RTPS Entity Model Mirrors — But Isn't Identical To — the DDS Entity Model

RTPS defines its own entity concepts that underlie the DDS API entities:

| DDS API entity | RTPS entity |
|---|---|
| DomainParticipant | RTPS **Participant** |
| DataWriter | RTPS **Writer** (an "Endpoint") |
| DataReader | RTPS **Reader** (an "Endpoint") |

Each RTPS entity is identified by a **GUID** (Globally Unique Identifier) — a
combination of a GUID prefix (identifying the participant, derived typically from
things like the host's IP and process ID) and an Entity ID (identifying the specific
writer/reader within that participant) — covered in depth in topic 19. This GUID is
what actually appears on the wire in RTPS messages, not any DDS-API-level object
reference.

---

## A Mental Model: DDS Sits "Above" RTPS

```
┌─────────────────────────────────────────────┐
│  Application code                            │
│  (write(), take(), QoS objects, listeners)   │
├─────────────────────────────────────────────┤
│  DDS API layer                               │
│  (DomainParticipant, Topic, DataWriter, ...) │
├─────────────────────────────────────────────┤
│  RTPS protocol layer                         │
│  (Participants, Writers, Readers, GUIDs,     │
│   discovery, reliability, submessages)       │
├─────────────────────────────────────────────┤
│  Transport layer                             │
│  (UDP/IP multicast+unicast, TCP, Shared      │
│   Memory — topic 27)                         │
└─────────────────────────────────────────────┘
```

An engineer debugging at the network level (e.g., with Wireshark's RTPS dissector —
topic 36) is looking directly at the RTPS layer — GUIDs, sequence numbers, submessages —
not at DDS-API-level constructs, which is why understanding RTPS concretely (not just
"DDS uses some protocol underneath") matters for serious troubleshooting.

---

## Vendor Interoperability in Practice

Because RTPS is a genuine interoperability standard (not just a shared "inspiration"),
different vendors' implementations are tested against each other at OMG-sponsored
interoperability events (a practice sometimes called "plugfests"), and most major
vendors publish interoperability compliance statements. That said, senior engineers
should be aware of practical caveats (expanded in topic 34):

- **RTPS covers the wire protocol, not every QoS behavior's exact semantics** — subtle
  differences in vendor-specific default values or edge-case behaviors can exist.
- **DDS Security and XTypes support/versions** can vary by vendor and release, affecting
  interoperability for those specific features even when basic pub-sub interoperates
  fine.
- **Vendor-proprietary QoS extensions** (features beyond the OMG standard) obviously
  don't interoperate with other vendors, by definition — use them deliberately and
  sparingly if cross-vendor interoperability is a requirement.

---

## Summary

RTPS (Real-Time Publish-Subscribe Protocol) is the OMG-standardized wire protocol that
underlies DDS, separating the **API/programming model** (DDS) from the **network wire
format** (RTPS) — a separation that enables genuine cross-vendor interoperability. RTPS
was designed to run natively over inherently unreliable, connectionless UDP/multicast
transport while layering optional, per-flow reliability on top via HEARTBEAT/ACKNACK,
avoiding any broker in the data path. Its entity model (Participants, Writers, Readers,
each identified by a GUID) underlies the DDS API entities you interact with in
application code, and understanding RTPS concretely — not just as an abstract "protocol
that exists" — is essential for real network-level troubleshooting and for reasoning
about vendor interoperability guarantees and their practical limits.

---

## Things to Remember

- **DDS is the API; RTPS is the wire protocol** — this separation is what enables genuine cross-vendor interoperability.
- **RTPS runs natively over UDP/multicast by default**, adding reliability as an optional protocol-level layer rather than depending on TCP.
- **RTPS is inherently peer-to-peer** — no broker ever sits in the data path.
- **RTPS entities (Participant, Writer, Reader) underlie DDS API entities** and are identified on the wire by GUIDs, not API object references.
- **Vendor interoperability is real but has practical caveats** — QoS edge-case behavior, DDS Security/XTypes version differences, and vendor-proprietary extensions can all affect true interoperability.
- **Network-level troubleshooting (e.g., Wireshark) operates at the RTPS layer** — knowing RTPS concretely, not just abstractly, is essential for this kind of debugging.
- **Treat vendor choice as revisable** when staying within standard RTPS/QoS behavior and avoiding proprietary extensions.
