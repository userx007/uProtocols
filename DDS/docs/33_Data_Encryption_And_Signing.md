# 33. Data Encryption and Signing

## Overview
The third pillar of DDS Security (after Authentication and Access Control) is the **Cryptographic Plugin**, responsible for actually protecting data in transit — both the discovery metadata exchanged via SPDP/SEDP and the user data flowing between matched writers and readers. This document covers per-topic protection kinds, message signing, and the key exchange overhead involved.

## Protection Kinds
DDS Security defines a spectrum of protection levels, configurable independently for different message categories via the Governance document:

| Kind | Effect |
|---|---|
| `NONE` | No cryptographic protection. |
| `SIGN` | Message is signed (integrity + authenticity) but sent in plaintext — tampering is detectable, but content is readable by anyone who can capture traffic. |
| `ENCRYPT` | Message is both encrypted and signed — confidential and tamper-evident. |
| `SIGN_WITH_ORIGIN_AUTHENTICATION` / `ENCRYPT_WITH_ORIGIN_AUTHENTICATION` | Adds per-sender key derivation so that origin can be cryptographically distinguished even among multiple writers of the same topic — useful when you need "which specific writer sent this" guarantees at the crypto layer, not just "signed by *a* legitimate writer."

These can be set at three independent granularities:
- **`rtps_protection_kind`** — protects the whole RTPS message envelope.
- **`discovery_protection_kind`** — protects SPDP/SEDP metadata specifically.
- **Per-topic `metadata_protection_kind` / `data_protection_kind`** — fine-grained control per topic, e.g., encrypting a sensitive `FleetCommand` topic while leaving a public `SystemHeartbeat` topic merely signed.

## Example: Per-Topic Configuration
```xml
<topic_access_rules>
  <topic_rule>
    <topic_expression>FleetCommand</topic_expression>
    <metadata_protection_kind>ENCRYPT</metadata_protection_kind>
    <data_protection_kind>ENCRYPT</data_protection_kind>
  </topic_rule>
  <topic_rule>
    <topic_expression>SystemHeartbeat</topic_expression>
    <metadata_protection_kind>SIGN</metadata_protection_kind>
    <data_protection_kind>SIGN</data_protection_kind>
  </topic_rule>
</topic_access_rules>
```
`FleetCommand` (e.g., "stop vehicle," "change route") gets full confidentiality + integrity; `SystemHeartbeat` (non-sensitive liveliness pings) only needs tamper detection, saving CPU cycles on encryption for a high-frequency, low-sensitivity topic.

## Key Exchange and Session Keys
- During the Authentication handshake (topic 32), a Diffie-Hellman exchange establishes a **shared secret** between each pair of authenticated participants.
- This shared secret seeds **symmetric session keys** (typically AES-GCM in the reference implementation) used for actual message encryption/signing — asymmetric crypto (expensive) is used only during the handshake, while the high-volume data path uses fast symmetric crypto.
- Session keys are periodically **rekeyed**, both on a schedule and in response to events like a reader leaving the group, to preserve forward secrecy for group communication.

## Example: Overhead Illustration
For a topic with `ENCRYPT` protection, each RTPS submessage carries additional overhead:
```
Plaintext CDR payload:         [ ... N bytes ... ]
Encrypted (AES-GCM) payload:   [ IV (12B) | ciphertext (N bytes) | auth tag (16B) ]
```
So roughly **28 extra bytes per message** for AES-GCM's IV and authentication tag, plus the CPU cost of the encryption operation itself — usually negligible per-message on modern hardware, but non-trivial in aggregate at very high message rates (tens of thousands of msgs/sec) or on constrained embedded CPUs.

## Group Key Distribution for Multi-Reader Topics
When multiple readers subscribe to the same encrypted topic, the writer doesn't necessarily encrypt separately per reader — a shared **group key** is distributed to all authorized readers so the writer can encrypt once and multicast/unicast the same ciphertext to all of them, preserving multicast delivery efficiency (topic 28) even under encryption.

## Common Pitfalls
- Applying `ENCRYPT` uniformly to every topic "to be safe," incurring unnecessary CPU/bandwidth overhead on high-frequency, non-sensitive topics — use `SIGN` or `NONE` where appropriate instead.
- Forgetting `discovery_protection_kind` — leaving discovery metadata unprotected can leak the entire system topology even when user data is fully encrypted.
- Not budgeting for rekey events in latency-sensitive systems — a rekey can introduce a brief processing pause for affected readers/writers.
- Assuming encryption alone provides authorization — encryption protects confidentiality/integrity, but Access Control (topic 32) is what actually decides who's allowed to read/write in the first place.

## Key Takeaways
- The Cryptographic Plugin offers a graduated spectrum (`NONE` → `SIGN` → `ENCRYPT`, with optional origin authentication) applicable independently to discovery metadata, RTPS envelopes, and per-topic data.
- Session keys are symmetric (fast) and derived from a per-pair DH exchange established once during the Authentication handshake.
- Group key distribution preserves multicast efficiency even for encrypted multi-reader topics.
- Choose protection levels **per topic** based on actual sensitivity and frequency — blanket encryption isn't always the right trade-off.
- Always protect discovery metadata, not just user data, or topology stays exposed to a passive eavesdropper.
