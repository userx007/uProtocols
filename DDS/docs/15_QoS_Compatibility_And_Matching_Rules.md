# 15. QoS Compatibility and Matching Rules

## Overview

Every individual QoS policy topic in this series (07–14) touched on its own
compatibility rule. This topic pulls that scattered knowledge together into one coherent
model, because understanding QoS matching **as a system** — not just policy by policy —
is what actually lets a senior engineer diagnose "why won't my writer and reader
connect?" quickly, instead of guessing. Silent non-matching due to QoS incompatibility
is, in practice, one of the most common classes of DDS production issues, precisely
*because* it fails silently: no exception, no error log by default — just a DataReader
that never receives data.

---

## The General Model: Requested vs. Offered

Most QoS policies in DDS follow a single conceptual pattern:

- A **DataWriter offers** a certain quality of service.
- A **DataReader requests** a certain quality of service.
- The two entities **match** only if, for **every** applicable policy, what's offered is
  "at least as good as" what's requested.

This is deliberately asymmetric — it models a real-world contract: a reader states its
*minimum* acceptable service level, and a writer that provides *more* than that minimum
still satisfies the reader. A writer providing *less* than the reader's minimum cannot.

```
IF for every policy P: writer.offered(P) >= reader.requested(P)
THEN the DataWriter and DataReader MATCH
ELSE they do NOT match, and both sides receive an incompatible-QoS status event
```

---

## Not Every Policy Follows "Offered ≥ Requested"

It's important to recognize that the requested/offered model has **three different
flavors** depending on the policy:

| Flavor | Policies | Rule |
|---|---|---|
| **Ordered ("offered ≥ requested")** | Reliability, Durability, Deadline, Liveliness, Latency Budget, Destination Order | Writer must offer a level *at least as strong/frequent* as what the reader requests. |
| **Exact match required** | Ownership | Writer and reader must specify the **identical** kind (`SHARED`/`SHARED` or `EXCLUSIVE`/`EXCLUSIVE`) — no "better" substitute is accepted. |
| **Set-intersection match** | Partition | Writer and reader match if their partition name sets **overlap at all** (topic 13) — not an ordering. |

Treating every policy as if it followed the same ordering rule is a common source of
confusion — e.g., assuming an `EXCLUSIVE`-ownership writer can satisfy a `SHARED`-
ownership reader "because exclusive sounds stronger" is simply wrong; Ownership requires
an exact match.

---

## Consolidated Compatibility Table

| Policy | Model | Compatible when |
|---|---|---|
| **Reliability** (07) | Ordered | `RELIABLE` offered satisfies any request; `BEST_EFFORT` offered only satisfies `BEST_EFFORT` requested |
| **Durability** (08) | Ordered | offered ≥ requested, on the scale `VOLATILE < TRANSIENT_LOCAL < TRANSIENT < PERSISTENT` |
| **Deadline** (10) | Ordered (numeric) | offered `period` ≤ requested `period` |
| **Liveliness** (11) | Ordered (kind) + numeric | offered kind ≥ requested kind (`AUTOMATIC < MANUAL_BY_PARTICIPANT < MANUAL_BY_TOPIC`) **and** offered `lease_duration` ≤ requested |
| **Ownership** (12) | Exact match | offered kind == requested kind |
| **Partition** (13) | Set intersection | any overlap between the two partition name sets (including both empty) |
| **Latency Budget** | Ordered (numeric) | offered `duration` ≤ requested `duration` |
| **Destination Order** | Ordered | offered ≥ requested (`BY_RECEPTION_TIMESTAMP < BY_SOURCE_TIMESTAMP`) |
| **Presentation** | Ordered (structural) | offered `access_scope` ≥ requested; offered `coherent_access`/`ordered_access` ≥ requested (booleans: true satisfies both true and false requests) |

(History, Resource Limits, and Lifespan/Time-Based Filter are **not** part of the
requested/offered matching process at all — they affect *what data* is retained or
delivered once matched, not *whether* matching occurs.)

---

## Detecting Incompatibility: The Status Events

When policies are incompatible, DDS does **not** throw an exception — it silently
declines to match, and instead notifies both sides via status events, which the
application must proactively check or listen for:

```cpp
class DiagnosticListener : public DataReaderListener {
    void on_requested_incompatible_qos(DataReader* reader,
                                        const RequestedIncompatibleQosStatus& status) override {
        std::cerr << "Reader failed to match a writer due to incompatible QoS. "
                  << "Last incompatible policy ID: " << status.last_policy_id << "\n";
        // status.policies[] gives a per-policy breakdown of how many incompatibilities occurred
    }
};

class WriterDiagnosticListener : public DataWriterListener {
    void on_offered_incompatible_qos(DataWriter* writer,
                                      const OfferedIncompatibleQosStatus& status) override {
        std::cerr << "Writer failed to match a reader due to incompatible QoS. "
                  << "Last incompatible policy ID: " << status.last_policy_id << "\n";
    }
};
```

`status.last_policy_id` identifies exactly **which** policy caused the most recent
incompatibility (e.g., `RELIABILITY_QOS_POLICY_ID`, `DURABILITY_QOS_POLICY_ID`), and
`status.policies` provides cumulative counts per policy — invaluable for pinpointing the
exact mismatch without guesswork.

---

## A Systematic Troubleshooting Checklist

When a DataReader isn't receiving data it should be, work through this in order:

1. **Check `on_subscription_matched` / `on_publication_matched` fired at all.** If
   neither side ever reports a match, you have a discovery or QoS problem — proceed to
   step 2. If they *did* match, the issue is elsewhere (e.g., History/Resource Limits
   dropping samples, filtering, or an application logic bug).
2. **Check `on_requested_incompatible_qos` / `on_offered_incompatible_qos`.** If these
   fired, `status.last_policy_id` tells you exactly which policy to fix.
3. **Verify Topic name and type compatibility** — a typo'd topic name or an
   incompatible/mismatched IDL type (topic 23/26) prevents matching before QoS is even
   evaluated.
4. **Verify Partition overlap** (topic 13) — a forgotten or mismatched partition setting
   is a very common, easily-overlooked cause.
5. **Verify Domain ID** (topic 02) — the most basic (and surprisingly common) mismatch;
   confirm both sides are actually on the same domain.
6. **Check DDS Security policies**, if enabled (topic 32) — access control rules can
   silently prevent matching independent of QoS compatibility.

---

## Designing QoS Profiles to Avoid Incompatibility by Construction

Rather than debugging mismatches after the fact, mature DDS deployments **standardize
QoS profiles** (topic 06's XML profile mechanism) so that writers and readers of a given
Topic are never configured independently and inconsistently in the first place:

```xml
<qos_profile name="VehiclePositionProfile" is_default_qos="false">
  <datawriter_qos base_name="VehiclePositionProfile::Base"/>
  <datareader_qos base_name="VehiclePositionProfile::Base"/>
</qos_profile>
```

Deriving both the DataWriter and DataReader QoS from a **shared base profile** for a
given Topic is a strong practice: it guarantees the reader's request can never exceed
what the writer offers, because both are generated from the same source of truth,
rather than being independently hand-tuned by different teams/services that may drift
out of sync over time.

---

## Summary

QoS compatibility governs whether a DataWriter and DataReader are allowed to match and
exchange data, and understanding it requires recognizing that different policies follow
different compatibility models: most (Reliability, Durability, Deadline, Liveliness,
Latency Budget, Destination Order) use an ordered "offered ≥ requested" rule; Ownership
requires an exact kind match; and Partition uses set intersection rather than any
ordering. Incompatibility fails silently from a data-flow perspective — no exception is
thrown — so proactively handling `on_requested_incompatible_qos` and
`on_offered_incompatible_qos`, and inspecting `status.last_policy_id`, is essential for
efficient troubleshooting. The most robust long-term defense against QoS mismatches is
architectural: deriving writer and reader QoS for a given Topic from a single shared
profile, so requested and offered service levels can never drift apart.

---

## Things to Remember

- **Most policies use "offered ≥ requested"**; Ownership requires an **exact** kind match; Partition uses **set intersection**, not an ordering — know which model applies to which policy.
- **Incompatible QoS fails silently** — no exception, just entities that never match; always monitor `on_requested_incompatible_qos`/`on_offered_incompatible_qos`.
- **`status.last_policy_id` tells you exactly which policy caused the mismatch** — use it instead of guessing.
- **History, Resource Limits, Lifespan, and Time-Based Filter are not part of matching** — they affect data content/delivery *after* a match, not whether a match occurs.
- **Follow a systematic troubleshooting order**: match status → incompatible-QoS status → topic/type → partition → domain → security.
- **Derive writer and reader QoS from a shared base profile** per Topic to prevent requested/offered drift by construction, rather than debugging mismatches reactively.
- **Domain ID and Partition mismatches are among the most common, easily-overlooked causes** of "no data arriving" — check them early, not last.
