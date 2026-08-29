# 31. DDS Security Overview

## Overview
The **DDS Security** specification (OMG, an extension to core DDS) adds authentication, access control, and encryption/signing to DDS without changing the core pub/sub programming model. It's built as a **pluggable architecture** — each security function is provided by an independently replaceable plugin, so organizations can use built-in reference implementations or bring their own (e.g., for FIPS compliance or custom PKI integration).

## The Five Plugin Categories
1. **Authentication Plugin** — verifies participant identity, typically via X.509 certificates and a shared Identity CA (see topic 32).
2. **Access Control Plugin** — enforces what a given (authenticated) participant may publish/subscribe to, using a **Governance** document (domain-wide security rules) and a **Permissions** document (per-participant grants) (topic 32).
3. **Cryptographic Plugin** — handles encryption and signing of both discovery metadata and user data (topic 33).
4. **Logging Plugin** — produces a security audit trail (who did what, matched/rejected connections, etc.).
5. **Data Tagging Plugin** — optionally tags individual samples with security labels for fine-grained downstream policy enforcement (less commonly used than the first four).

## How the Pieces Fit Together
```
                +--------------------+
                |  Identity CA        |  (issues participant certs)
                +--------------------+
                          |
                          v
Participant A <--Authentication--> Participant B
       |                                    |
       v                                    v
  Access Control (Governance + Permissions) checked per-topic
       |                                    |
       v                                    v
   Cryptographic Plugin encrypts/signs discovery + user data
```

## Example: A Secured Deployment Layer Cake
1. **PKI setup**: Generate an Identity CA and issue X.509 certificates + private keys to each participant.
2. **Governance document**: Domain-wide XML specifying, e.g., "all topics in this domain require encryption and access control by default," signed by a Permissions CA.
3. **Permissions document**: Per-participant XML granting specific publish/subscribe rights to specific topics/partitions, also signed.
4. **QoS activation**: Each `DomainParticipant` is configured with property QoS pointing to its identity certificate, private key, governance file, and permissions file.

```xml
<domain_participant_qos>
  <property>
    <value>
      <element>
        <name>dds.sec.auth.identity_ca</name>
        <value>file:///certs/identity_ca.pem</value>
      </element>
      <element>
        <name>dds.sec.auth.identity_certificate</name>
        <value>file:///certs/participant1_cert.pem</value>
      </element>
      <element>
        <name>dds.sec.auth.private_key</name>
        <value>file:///certs/participant1_key.pem</value>
      </element>
      <element>
        <name>dds.sec.access.governance</name>
        <value>file:///certs/governance.p7s</value>
      </element>
      <element>
        <name>dds.sec.access.permissions</name>
        <value>file:///certs/permissions_participant1.p7s</value>
      </element>
    </value>
  </property>
</domain_participant_qos>
```

## Backward Compatibility
A key design goal: a domain can contain a **mix** of secured and unsecured participants during migration, with Governance rules controlling whether unsecured participants are allowed to join at all, or only for specific (non-sensitive) topics.

## Common Pitfalls
- Treating DDS Security as "encryption only" — access control (who can publish/subscribe to what) is equally, if not more, important in most threat models.
- Underestimating the operational burden of certificate lifecycle management (issuance, rotation, revocation) at fleet scale.
- Leaving discovery metadata unencrypted while encrypting user data — an attacker can still map out the entire topic/participant topology from unprotected SPDP/SEDP traffic if this isn't configured correctly.
- Performance-testing without security enabled, then being surprised by the CPU/latency cost of per-message crypto operations once security is turned on in production.

## Key Takeaways
- DDS Security is a pluggable, standards-based extension covering authentication, access control, encryption/signing, logging, and (optionally) data tagging.
- Governance + Permissions documents, both cryptographically signed, define the domain-wide and per-participant security policy respectively.
- Discovery traffic itself can and should be protected, not just user data — otherwise topology mapping is trivial for an eavesdropper.
- Mixed secured/unsecured domains are supported for migration but must be deliberately governed.
- Budget for both the PKI/certificate operational overhead and the runtime CPU cost of cryptographic operations when planning a secured deployment.
