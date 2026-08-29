# 18. Sequence Numbers, Heartbeats and ACKNACK

## Overview

Topic 17 introduced HEARTBEAT and ACKNACK as submessage kinds. This topic goes deep on
the **mechanism itself** — the actual state machine and algorithm that implements
`RELIABLE` reliability (topic 07) over an inherently unreliable UDP transport. This is
the single most important protocol-level mechanism to understand deeply, because nearly
every "reliable delivery is slow/stuck/not working" production issue is ultimately a
question about sequence numbers, heartbeat cadence, or ACKNACK behavior.

---

## Sequence Numbers: The Foundation

Every sample a DataWriter publishes is assigned a **monotonically increasing sequence
number**, scoped **per DataWriter** (not per instance, not per topic globally):

```
Writer X's sequence numbers: 1, 2, 3, 4, 5, 6, ...
```

This applies **regardless of which instance** each sample belongs to — if a writer
publishes samples for instance A, then instance B, then instance A again, the sequence
numbers still increment monotonically across all of them (1, 2, 3, ...), not
per-instance. Sequence numbers are what allow a reader to precisely identify **which**
samples it has received and which are missing — the entire reliability mechanism is
built on tracking gaps in this per-writer sequence.

---

## The Writer's Perspective: What It Tracks

A reliable DataWriter maintains, conceptually, a **history cache** — the set of samples
it currently holds, bounded by History and Resource Limits QoS (topic 09) — along with,
for each matched reliable reader, knowledge of:

- The **highest sequence number that reader has acknowledged**.
- Any specific sequence numbers that reader has explicitly **nacked** (reported missing).

This per-reader state is what allows a writer serving multiple readers at different
"catch-up" points to retransmit the right data to the right reader, rather than
broadcasting a one-size-fits-all retransmission.

---

## HEARTBEAT: "Here's What I Have"

A reliable writer periodically sends a `HEARTBEAT` submessage announcing the range of
sequence numbers currently in its history cache:

```
HEARTBEAT: firstSN = 42, lastSN = 107
  → "I currently hold samples numbered 42 through 107 (inclusive)."
```

Key behaviors:

- Sent **periodically** at an interval configurable via the writer's Protocol QoS
  (commonly a few hundred milliseconds by default, vendor-dependent) — this is a direct
  latency/overhead trade-off: more frequent heartbeats mean faster gap detection and
  retransmission, at the cost of more network/CPU overhead.
- Also sent **on-demand** in many implementations, e.g., immediately after a `write()`
  call, to minimize latency for the common "no loss occurred" case rather than waiting
  for the next periodic interval.
- Can be marked with a `FINAL` flag, indicating the writer does **not** require an
  ACKNACK response unless the reader actually has something to report — an optimization
  to avoid unnecessary ACKNACK traffic when nothing is wrong.

---

## ACKNACK: "Here's What I'm Missing"

Upon receiving a HEARTBEAT (or periodically/proactively), a reliable reader responds
with an `ACKNACK` submessage:

```
ACKNACK: readerSNState = { base = 105, bitmap = [not 105, missing 106] }
  → "I have everything up through 104. I'm missing 106 specifically
     (and haven't yet received 105 as expressed by the bitmap encoding)."
```

The ACKNACK's sequence-number-set representation efficiently encodes both:

- A **base** sequence number (everything below this has been fully received).
- A **bitmap** identifying specific gaps at or above that base.

This compact encoding lets a reader with, say, one missing sample out of ten thousand
communicate that gap efficiently, without listing all ten thousand acknowledged numbers
individually.

### Retransmission

Upon receiving an ACKNACK reporting gaps, the writer retransmits **only the specifically
nacked sequence numbers** via targeted `DATA` submessages — not the entire history — an
important efficiency property that keeps retransmission cost proportional to actual
loss, not to total traffic volume.

```
Writer receives ACKNACK naming missing SN 106
  → Writer resends DATA submessage for SN 106 only (unicast to that specific reader)
```

---

## The Full Reliable Exchange, End to End

```
Time →

Writer:  write() write() write()                 [periodic]
         DATA(1)  DATA(2)  DATA(3)  ...  HEARTBEAT(first=1,last=3)
                                                        │
                                                        ▼
Reader:                            (received 1, 3 — missed 2 due to packet loss)
                                    ACKNACK(base=2, bitmap=[missing 2])
                                                        │
                                                        ▼
Writer:                            DATA(2)  [targeted retransmission]
                                                        │
                                                        ▼
Reader:                            (now has 1,2,3 — fully caught up)
                                    ACKNACK(base=4, bitmap=[]) [or no gaps to report]
```

This closely resembles TCP's sliding-window acknowledgment/retransmission philosophy —
but crucially, it's applied **per matched writer/reader pair, per sample**, over a
connectionless transport, and integrated with DDS's richer instance/QoS model (e.g.,
a `GAP` submessage can tell a reader "sequence number 2 will never be resent" if it was
explicitly disposed or expired via Lifespan QoS before retransmission could occur —
topic 14).

---

## Heartbeat/ACKNACK Tuning: A Real Latency/Overhead Trade-off

This mechanism exposes several tunable parameters (via `RTPS Reliability Protocol` QoS,
vendor-specific naming varies) with direct operational consequences:

| Parameter | Effect of increasing it | Effect of decreasing it |
|---|---|---|
| **Heartbeat period** | Slower gap detection/retransmission → higher worst-case latency on loss | Faster gap detection → lower loss-recovery latency, but more network/CPU overhead |
| **`heartbeat_response_delay`** (reader's delay before sending ACKNACK) | Reduces ACKNACK "storm" risk when many readers respond to one heartbeat simultaneously | Faster acknowledgment, but higher risk of many readers responding at once, especially with many readers on a shared multicast group |
| **`nack_response_delay`** (writer's delay before responding to an ACKNACK) | Allows batching multiple readers' nacks before retransmitting, reducing redundant retransmission | Faster retransmission response, but potentially more redundant/duplicate retransmissions |

**Practical implication:** for hard real-time systems where worst-case loss-recovery
latency matters, heartbeat period and response delays should be tuned aggressively
tighter than defaults; for large-fan-out systems with many readers (e.g., hundreds of
subscribers to one topic), overly aggressive settings can cause "ACKNACK storms" that
themselves degrade network performance — this is a genuine tuning trade-off, not a
"lower is always better" setting.

---

## Practical Relevance for Engineers

- **A Wireshark capture showing frequent, repeated HEARTBEAT/ACKNACK cycles with nacked
  sequence numbers** is direct evidence of packet loss on the network — a strong signal
  to investigate network quality, multicast configuration, or NIC/driver issues rather
  than application logic.
- **Unexpectedly high latency on a "reliable" topic** is often explainable by heartbeat
  period being too coarse for the required responsiveness — tightening it is a direct,
  well-understood lever.
- **Large fan-out reliable topics (many readers)** require careful heartbeat/ACKNACK
  tuning to avoid overwhelming the writer or network with acknowledgment traffic —
  this is a first-order scalability concern for reliable pub-sub at scale.

---

## Summary

DDS's `RELIABLE` reliability is implemented via per-writer monotonic sequence numbers,
periodic (and on-demand) `HEARTBEAT` announcements of the writer's available sequence
range, reader-driven `ACKNACK` responses that efficiently encode acknowledged versus
missing sequence numbers via a base-plus-bitmap representation, and targeted,
per-sample retransmission of only the specifically nacked data — a mechanism
philosophically similar to TCP's sliding window, but connectionless, per-sample, and
layered over UDP. The heartbeat period and various response-delay parameters expose a
genuine, non-trivial latency-versus-overhead trade-off that senior engineers must tune
deliberately based on real-time requirements and expected reader fan-out, and this
mechanism is the concrete, wire-level thing to inspect (e.g., via Wireshark) when
diagnosing reliability-related latency or throughput issues in production.

---

## Things to Remember

- **Sequence numbers are per-writer and monotonic**, spanning all instances that writer publishes — not per-instance, not per-topic-globally.
- **HEARTBEAT = "here's what I have"** (writer → readers, periodic + on-demand); **ACKNACK = "here's what I'm missing"** (reader → writer, in response).
- **ACKNACK uses an efficient base+bitmap encoding**, so reporting a few gaps out of thousands of samples is cheap.
- **Retransmission is targeted** — only the specifically nacked sequence numbers are resent, not the entire history.
- **`GAP` submessages tell a reader "this sequence number will never be resent"** (e.g., disposed or expired via Lifespan) — preventing indefinite waiting for unrecoverable data.
- **Heartbeat period and response-delay parameters are a real latency-vs-overhead trade-off** — tune aggressively for hard real-time, more conservatively for large reader fan-out to avoid ACKNACK storms.
- **This is the concrete mechanism to inspect via Wireshark** when diagnosing reliability-related latency, throughput, or "stuck" delivery issues.
