
## Additional Protocol-Level Topics

**SCTP (Stream Control Transmission Protocol)** - Multi-streaming transport protocol combining TCP reliability with UDP-like message boundaries

**DCCP (Datagram Congestion Control Protocol)** - Congestion-controlled unreliable datagrams for real-time applications

**IP Fragmentation and Reassembly** - How packets are split and reconstructed across the network path

**ARP and Neighbor Discovery** - Address resolution for IPv4 (ARP) and IPv6 (NDP)

**IGMP and MLD** - Internet Group Management Protocol for IPv4 multicast and Multicast Listener Discovery for IPv6

## Modern Network Programming

**eBPF and XDP** - Extended BPF for programmable packet processing and high-performance filtering

**AF_XDP Sockets** - Zero-copy fast path between NIC and userspace

**DPDK (Data Plane Development Kit)** - Bypass kernel networking stack for ultra-low latency

**io_uring advanced patterns** - Beyond basics: linked operations, buffer selection, multishot operations

## Practical Implementation

**Backpressure and Flow Control** - Application-level strategies to prevent memory exhaustion

**Graceful Degradation** - Handling partial network failures and timeouts

**Circuit Breakers** - Preventing cascade failures in distributed systems

**Retry Strategies** - Exponential backoff, jitter, and retry budgets

**Socket Splice and Proxy Patterns** - Efficient data forwarding without userspace copies

## Observability and Monitoring

**Socket Statistics (ss -i)** - TCP metrics, congestion window, RTT measurements

**Performance Counters** - Network interface statistics and dropped packets

**Tracing Network Calls** - Using eBPF/bpftrace for performance analysis

**Connection Tracking** - Netfilter conntrack and stateful inspection

## Specialized Topics

**Multipath TCP (MPTCP)** - Using multiple network paths simultaneously

**BBR Congestion Control** - Google's bottleneck bandwidth and RTT-based algorithm

**TCP Fast Open (TFO)** - Reducing connection establishment latency

**SO_REUSEPORT and Load Distribution** - Kernel-level load balancing across processes

**Receive Side Scaling (RSS/RPS)** - Multi-queue NIC handling and CPU affinity

**Socket Memory Accounting** - Understanding sk_buff, rmem, wmem limits

