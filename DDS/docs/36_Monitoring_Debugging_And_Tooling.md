# 36. Monitoring, Debugging and Tooling

## Overview
Because DDS is decentralized (no broker to inspect) and much of its behavior — discovery, matching, QoS negotiation — happens invisibly beneath the application layer, dedicated tooling is essential for debugging real systems. This document covers the primary categories: wire-level inspection, admin/monitoring consoles, and built-in introspection topics.

## Wireshark RTPS Dissector
Wireshark ships with a built-in **RTPS dissector** that decodes DDS wire traffic directly — showing SPDP/SEDP announcements, DATA/HEARTBEAT/ACKNACK submessages, and (with the type registered or PL_CDR parameter lists) actual sample field contents.

### Example Workflow
```bash
# Capture UDP multicast discovery + data traffic on a domain
sudo tcpdump -i eth0 -w capture.pcap 'udp port 7400 or udp portrange 7410-7420'

# Open in Wireshark, filter to RTPS traffic:
rtps
# Narrow to a specific submessage type:
rtps.sm.id == 0x15   # ACKNACK submessages, useful for reliability debugging
```
This is the ground-truth tool for answering "is data actually leaving the writer's socket, and in what shape?" — independent of any vendor's own claims.

## Admin Consoles
Most vendors provide a GUI or web-based **admin console** for live introspection of a running system:
- **RTI Admin Console / RTI Monitor** — visualizes discovered participants, endpoints, QoS, matching status, and (with monitoring library instrumentation) live throughput/latency graphs per DataWriter/DataReader.
- **Eclipse Cyclone DDS** — offers `ddsperf` for performance testing and integrates with generic DDS spy tools; visualization typically via third-party or custom tooling.
- **eProsima Fast DDS** — provides `fastdds discovery` CLI tooling and Fast DDS Monitor, a dedicated GUI for live topology/QoS visualization.

These tools typically answer questions like: "Are these two endpoints actually matched?" "What QoS did each side actually request/offer?" "Is this writer's queue backing up?"

## Built-In Topics
DDS mandates a set of **built-in topics** that expose the discovery database itself as ordinary (read-only) DDS topics an application can subscribe to:

| Built-in Topic | Exposes |
|---|---|
| `DCPSParticipant` | Discovered participants (SPDP data) |
| `DCPSPublication` | Discovered DataWriters (SEDP data) |
| `DCPSSubscription` | Discovered DataReaders (SEDP data) |
| `DCPSTopic` | Discovered Topic definitions (vendor-optional) |

### Example: Programmatic Discovery Introspection
```cpp
Subscriber builtinSub = participant.builtin_subscriber();
DataReader<ParticipantBuiltinTopicData> partReader =
    builtinSub.lookup_datareader("DCPSParticipant");

auto samples = partReader.read();
for (auto& s : samples) {
  std::cout << "Discovered participant: " << s.data().key()
            << " from vendor: " << s.data().vendor_id() << std::endl;
}
```
This lets an application build its own live topology dashboard, health-check tool, or custom alerting entirely using standard DDS APIs — no vendor-specific tooling required.

## Status Conditions and Listeners for Diagnostics
Beyond dedicated tools, applications should actively use DDS's built-in status/listener mechanisms for early problem detection:
```cpp
class DiagListener : public DataWriterListener<VehicleStatus> {
  void on_offered_incompatible_qos(DataWriter<VehicleStatus>& writer,
                                    const OfferedIncompatibleQosStatus& status) override {
    std::cerr << "QoS mismatch! Policy: " << status.last_policy_id()
              << " total incompatible matches: " << status.total_count() << std::endl;
  }
  void on_publication_matched(DataWriter<VehicleStatus>& writer,
                               const PublicationMatchedStatus& status) override {
    std::cout << "Matched readers: " << status.current_count() << std::endl;
  }
};
```
`on_offered_incompatible_qos` is one of the single most useful diagnostic callbacks in DDS — it fires precisely when a writer and reader were discovered but *refused* to match due to QoS incompatibility, directly pinpointing the offending policy.

## Example: A Debugging Checklist
1. Confirm both sides appear in `DCPSParticipant` — if not, it's an SPDP/network problem (multicast, firewall).
2. Confirm the endpoint appears in `DCPSPublication`/`DCPSSubscription` on the peer — if not, SEDP delivery/reliability issue.
3. Check `on_offered_incompatible_qos` / `on_requested_incompatible_qos` listener callbacks for a QoS mismatch.
4. Verify topic name, registered type name, and partition strings match exactly.
5. Use Wireshark to confirm samples are actually on the wire if all of the above look correct but data still isn't arriving.

## Key Takeaways
- Wireshark's RTPS dissector is the vendor-neutral ground truth for wire-level debugging.
- Vendor admin consoles (RTI Admin Console/Monitor, Fast DDS Monitor, etc.) provide live, higher-level topology and performance visualization.
- Built-in topics (`DCPSParticipant`, `DCPSPublication`, `DCPSSubscription`) expose the discovery database via the standard DDS API itself — useful for custom tooling.
- The `on_offered_incompatible_qos` / `on_requested_incompatible_qos` listener callbacks are the fastest way to pinpoint QoS mismatch bugs.
- A systematic debugging checklist (participant → endpoint → QoS → wire) resolves the large majority of real-world "why won't my data flow" issues.
