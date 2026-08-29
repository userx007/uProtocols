# 27. Transport Configuration

## Overview
RTPS is transport-agnostic by design — it defines how *messages* are structured, not how *bytes* physically move. DDS implementations plug in one or more transports underneath RTPS, and choosing/tuning the right transport(s) for a given deployment is one of the most consequential engineering decisions in a DDS system.

## The Main Transports

### UDPv4 / UDPv6 (Default)
The default and most common transport. Connectionless, low overhead, supports both unicast and multicast natively — which is exactly what SPDP/SEDP and efficient fan-out delivery need.
```xml
<transport_builtin>
  <mask>UDPv4</mask>
</transport_builtin>
```
Trade-off: UDP has no built-in delivery guarantee or congestion control — DDS's own RELIABLE QoS layer (ACKNACK, retransmission) compensates for this at the RTPS level.

### TCP
Used when multicast/UDP is blocked or unreliable — notably across NAT boundaries, corporate firewalls, or certain cloud networking setups. Provides in-order, connection-oriented delivery at the transport layer, but:
- No native multicast (fan-out must be done via multiple TCP connections).
- Higher latency variability under packet loss (head-of-line blocking).
- Requires explicit peer addressing (no multicast-based discovery).

```xml
<transport_builtin>
  <mask>TCPv4</mask>
</transport_builtin>
<property>
  <value>
    <element>
      <name>dds.transport.TCPv4.tcp1.server_bind_port</name>
      <value>7400</value>
    </element>
  </value>
</property>
```

### Shared Memory (SHMEM)
For DataWriters and DataReaders **co-located on the same host**, SHMEM bypasses the network stack entirely, writing samples directly into a shared memory segment. This drastically reduces latency (microseconds instead of the sub-millisecond-to-millisecond range typical of UDP loopback) — critical for high-frequency intra-host pub/sub (e.g., robotics control loops).
```xml
<transport_builtin>
  <mask>SHMEM|UDPv4</mask>
</transport_builtin>
```
Most vendors will automatically prefer SHMEM over UDP for co-located endpoints when both are enabled, since locators are exchanged during discovery and the "best" reachable transport is chosen per-pair.

## Example: Multi-Transport Locator Negotiation
A participant can advertise locators for multiple transports simultaneously during SPDP/SEDP:
```
Locators for Participant A:
  SHMEM: shmem://12345
  UDPv4:  udpv4://192.168.1.10:7411
  TCPv4:  tcpv4://192.168.1.10:7400
```
A remote participant on the same host picks SHMEM; a remote participant on the same LAN picks UDPv4; a remote participant behind a firewall that blocks UDP picks TCPv4 — all transparently, without any application code changes.

## Example: Interface Binding
Multi-homed hosts often need to restrict which NICs DDS uses:
```xml
<property>
  <value>
    <element>
      <name>dds.transport.UDPv4.builtin.parent.allow_interfaces</name>
      <value>eth0</value>
    </element>
  </value>
</property>
```

## Common Pitfalls
- Enabling only UDP in environments (cloud VPCs, Kubernetes) where multicast and sometimes even broad UDP is restricted — leads to silent discovery failure (see topic 22 for the initial-peers workaround).
- Forgetting that TCP transport requires explicitly-addressed peers since it has no multicast-based auto-discovery.
- Not tuning UDP socket buffer sizes (`send_socket_buffer_size` / `recv_socket_buffer_size`) under high-throughput workloads — default OS buffer sizes are often too small and cause silent packet drops.
- Assuming SHMEM "just works" across containers — container network namespaces can isolate shared memory segments, requiring explicit shared-mount configuration.

## Key Takeaways
- RTPS is transport-agnostic; DDS vendors plug in UDP, TCP, and SHMEM (among others) underneath it.
- UDP (uni/multicast) is the default and best fit for most LAN deployments; TCP is the fallback for NAT/firewall-restricted networks; SHMEM is the low-latency choice for co-located endpoints.
- Multiple transports can be enabled simultaneously — DDS automatically selects the best reachable one per writer/reader pair via locator negotiation during discovery.
- Socket buffer sizing and NIC binding are common, high-impact tuning knobs, especially under load.
- Container/cloud networking often breaks multicast and sometimes SHMEM assumptions — plan transport configuration explicitly for those environments.
