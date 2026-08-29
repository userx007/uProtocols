# 20. Simple Participant Discovery Protocol (SPDP)

## Overview
SPDP is the mechanism by which DDS `DomainParticipant`s find each other on a network *before* any topic-level matching can happen. It is the first, coarsest layer of the two-tier discovery system defined by RTPS (the second tier being SEDP, covered in the next document). Without SPDP, no participant would know that any other participant exists — there would be nothing to exchange endpoint metadata with.

## Core Idea
Each `DomainParticipant` periodically announces itself by sending a special RTPS message — the **SPDP announcement**, carried as a `DATA` submessage on a well-known built-in topic (`DCPSParticipant`). This announcement is essentially the participant's business card. It contains:

- The participant's GUID prefix (globally unique identifier)
- Supported RTPS protocol version and vendor ID
- The unicast/multicast locators (IP:port pairs) where it can be reached
- Default QoS values for its future endpoints
- Lease duration (how long other participants should consider it "alive" without a fresh announcement)
- Available built-in endpoints (whether it supports SEDP publication/subscription readers/writers, etc.)

## How It Works, Step by Step
1. **Announcement**: On startup, a participant sends an SPDP announcement to a well-known **multicast address** (default `239.255.0.1`, port derived from the Domain ID) and/or to a list of configured unicast peers.
2. **Periodic re-announcement**: The participant repeats this announcement at a configurable interval (commonly every 1–3 seconds), refreshing its lease.
3. **Reception**: Any participant listening on that multicast group receives the announcement and adds the new participant to its local discovery database.
4. **Liveliness / lease expiration**: If a participant stops announcing for longer than its declared lease duration, peers consider it gone and clean up all associated state (endpoints, matches).
5. **Handshake, not negotiation**: SPDP is one-way, best-effort, and asynchronous — there's no acknowledgment protocol at this layer (that granularity belongs to SEDP and to reliable data delivery once matched).

## Example: Domain ID and Port Derivation
DDS derives well-known multicast/unicast ports from the Domain ID using a formula (per the RTPS spec):

```
PB = 7400                     # Port Base
DG = 250                      # Domain Gain
d0 = 0                        # SPDP multicast offset

SPDP multicast port = PB + DG * domainId + d0
```

For Domain ID `0`, this yields port `7400`. For Domain ID `5`, it yields `8650`. This is why two applications on the *same* Domain ID automatically discover each other on the same machine or LAN — they're listening on the same multicast port — while different Domain IDs are cleanly isolated at the network level.

## Example: Minimal Config (Connext-style pseudocode)
```xml
<domain_participant_qos>
  <discovery_config>
    <participant_liveliness_lease_duration>
      <sec>10</sec>
    </participant_liveliness_lease_duration>
    <participant_liveliness_assert_period>
      <sec>3</sec>
    </participant_liveliness_assert_period>
  </discovery_config>
</domain_participant_qos>
```
Here, the participant re-asserts itself every 3 seconds, and peers will drop it if nothing is heard for 10 seconds.

## Common Pitfalls
- **Multicast disabled on the network** (common in cloud/VPC environments) → SPDP never completes; must fall back to a static/unicast peer list.
- **Lease duration too short** relative to CPU load or network jitter → false "participant lost" events and discovery flapping.
- **Too many participants** on a flat multicast domain → SPDP traffic grows, contributing to the O(n²) discovery scalability problem (see topic 22).

## Key Takeaways
- SPDP discovers **participants**, not topics or endpoints — it's the coarse first layer.
- It relies on periodic, best-effort multicast (or configured unicast) announcements carried on the built-in `DCPSParticipant` topic.
- Liveliness is lease-based: silence beyond the lease duration means the peer is presumed dead.
- Multicast availability and lease tuning are the two most common sources of real-world SPDP problems.
- SPDP existing successfully is the *precondition* for SEDP (endpoint-level discovery) to even begin.
