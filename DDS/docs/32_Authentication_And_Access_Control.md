# 32. Authentication and Access Control

## Overview
This document drills into the first two of DDS Security's plugin categories (introduced in topic 31): **Authentication** (proving *who* a participant is) and **Access Control** (deciding *what* an authenticated participant may do). Together they form the identity and authorization backbone of a secured DDS domain.

## Authentication: Identity CA and Certificates
The default DDS Security authentication plugin (`DDS:Auth:PKI-DH`) uses:
- An **Identity CA** — a trusted root certificate authority that signs every participant's identity certificate.
- Per-participant **X.509 certificates** and private keys.
- A **Diffie-Hellman key exchange** during the handshake, which also establishes the shared secret used to derive session keys for the Cryptographic plugin (topic 33).

### Handshake Flow (Simplified)
```
Participant A                          Participant B
    | --- SPDP announcement (unsecured) --->  |
    | <---------- SPDP announcement --------- |
    | --- Handshake Request (cert + DH) ---->  |
    | <--- Handshake Reply (cert + DH) ------  |
    | --- Handshake Final (verified) ------->  |
    |         [Mutual authentication complete]  |
```
Both sides validate the peer's certificate against the shared Identity CA before proceeding — if validation fails, no further discovery or data exchange occurs with that peer.

## Access Control: Governance + Permissions
Once authenticated, **what** a participant may do is governed by two signed XML documents:

### Governance Document (Domain-Wide Policy)
```xml
<domain_rule>
  <domains>
    <id_range><min>0</min><max>0</max></id_range>
  </domains>
  <allow_unauthenticated_participants>false</allow_unauthenticated_participants>
  <enable_join_access_control>true</enable_join_access_control>
  <discovery_protection_kind>ENCRYPT</discovery_protection_kind>
  <liveliness_protection_kind>SIGN</liveliness_protection_kind>
  <rtps_protection_kind>ENCRYPT</rtps_protection_kind>
  <topic_access_rules>
    <topic_rule>
      <topic_expression>VehicleStatus</topic_expression>
      <enable_discovery_protection>true</enable_discovery_protection>
      <enable_read_access_control>true</enable_read_access_control>
      <enable_write_access_control>true</enable_write_access_control>
      <metadata_protection_kind>ENCRYPT</metadata_protection_kind>
      <data_protection_kind>ENCRYPT</data_protection_kind>
    </topic_rule>
  </topic_access_rules>
</domain_rule>
```
This sets the *default* security posture for the domain and specific topics — e.g., mandating encryption for the `VehicleStatus` topic specifically.

### Permissions Document (Per-Participant Grants)
```xml
<permissions>
  <grant name="Participant1Grant">
    <subject_name>CN=vehicle-101,O=FleetCorp</subject_name>
    <validity>
      <not_before>2026-01-01T00:00:00</not_before>
      <not_after>2027-01-01T00:00:00</not_after>
    </validity>
    <allow_rule>
      <domains><id>0</id></domains>
      <publish>
        <topics><topic>VehicleStatus</topic></topics>
      </publish>
      <subscribe>
        <topics><topic>FleetCommand</topic></topics>
      </subscribe>
    </allow_rule>
    <deny_rule>
      <domains><id>0</id></domains>
      <publish>
        <topics><topic>*</topic></topics>
      </publish>
    </deny_rule>
    <default>DENY</default>
  </grant>
</permissions>
```
Note the pattern: an explicit `allow_rule` grants specific publish/subscribe rights, a `deny_rule` can explicitly block others, and `default: DENY` ensures anything not explicitly allowed is refused — a standard **least-privilege, default-deny** security posture.

## Example: Rejecting an Unauthorized Publisher
If `vehicle-101`'s permissions only allow publishing `VehicleStatus` but its application code mistakenly tries to publish on `FleetCommand`, the Access Control plugin rejects the DataWriter's creation (or blocks matching) locally — the middleware itself enforces the policy without needing the application to self-police.

## Common Pitfalls
- Setting `allow_unauthenticated_participants: true` "temporarily" for debugging and forgetting to revert it before production deployment.
- Overly broad topic expressions (`*`) in permissions grants, defeating the purpose of least-privilege access control.
- Certificate expiration (`not_after`) silently disconnecting participants in long-running deployments if renewal isn't automated.
- Misunderstanding that Access Control decisions are enforced **locally** by each participant based on its own copy of the governance/permissions documents — a compromised or misconfigured single participant can only be as dangerous as its own signed permissions allow.

## Key Takeaways
- Authentication establishes verified identity via X.509 certs and a shared Identity CA, using a DH-based handshake that also seeds session key material.
- Access Control is driven by two signed documents: a domain-wide **Governance** policy and per-participant **Permissions** grants.
- Best practice is default-deny with narrowly-scoped allow rules per participant/topic.
- Both documents are cryptographically signed to prevent tampering, and validated locally by each participant.
- Certificate and permission lifecycle management (issuance, rotation, expiration) is an ongoing operational responsibility, not a one-time setup task.
