# 29. Performance Tuning and Batching

## Overview
DDS is used in domains ranging from sub-millisecond trading systems to high-throughput sensor fusion pipelines, and the "right" performance configuration differs enormously by use case. This document covers the primary knobs: batching, send queues, thread pools, and the fundamental latency-vs-throughput trade-off.

## The Latency vs. Throughput Trade-off
- **Low-latency configurations** send each sample immediately, minimizing per-sample delay but maximizing per-sample overhead (packet headers, syscalls, interrupts).
- **High-throughput configurations** accumulate multiple samples before sending, amortizing overhead across a batch — at the cost of added latency for the first samples in the batch (they wait for the batch to fill or a timer to expire).

There is no configuration that optimizes both simultaneously; tuning is about picking the right point on this curve for your workload.

## Batching QoS
Batching groups multiple small samples into a single larger network packet before sending.
```xml
<datawriter_qos>
  <batch>
    <enable>true</enable>
    <max_samples>50</max_samples>
    <max_data_bytes>8192</max_data_bytes>
    <max_flush_delay>
      <sec>0</sec>
      <nanosec>5000000</nanosec>   <!-- 5ms max wait before flush -->
    </max_flush_delay>
  </batch>
</datawriter_qos>
```
- `max_samples` / `max_data_bytes`: batch flushes once either limit is hit.
- `max_flush_delay`: a ceiling on how long a partially-full batch will wait before being sent anyway — this bounds the worst-case added latency.

**Example impact**: For a system publishing 10,000 small (50-byte) samples/sec, sending each individually might mean 10,000 UDP packets/sec (each with ~28+ bytes of IP/UDP header plus RTPS overhead) — batching 50 samples per packet cuts packet count by 50x, at a worst-case added latency equal to `max_flush_delay`.

## Send Queues and Asynchronous Publishing
Rather than blocking the calling thread on `write()` until the sample is fully serialized and handed to the OS socket, many implementations offer an **asynchronous publisher** mode with a dedicated send queue and background flushing thread:
```xml
<datawriter_qos>
  <publish_mode>
    <kind>ASYNCHRONOUS_PUBLISH_MODE_QOS</kind>
    <flow_controller_name>DDS_DEFAULT_FLOW_CONTROLLER_NAME</flow_controller_name>
  </publish_mode>
</datawriter_qos>
```
This decouples the application's `write()` call latency from actual network send latency — critical for applications that can't tolerate blocking on I/O in their hot path (e.g., a control loop thread).

## Thread Pools and Receive Processing
Reader-side receive processing (deserialization, listener callback dispatch) can be single-threaded (simple, ordered) or backed by a **thread pool** (parallel dispatch across many topics/instances):
```xml
<receiver_pool>
  <thread_pool_size>4</thread_pool_size>
</receiver_pool>
```
More threads increase throughput for CPU-bound deserialization/processing workloads but add complexity around ordering guarantees and can increase context-switch overhead if over-provisioned relative to available cores.

## Flow Controllers
Flow controllers **rate-limit** a writer's outbound bandwidth, useful for preventing a fast writer from overwhelming slower readers or shared network links:
```xml
<property>
  <name>dds.flow_controller.token_bucket.my_limiter.token_bucket.max_tokens</name>
  <value>1000000</value>  <!-- bytes/sec -->
</property>
```

## Example: Tuning Checklist for a High-Throughput Pipeline
1. Enable batching with a small `max_flush_delay` (e.g., 1–5ms) tuned to the acceptable added latency.
2. Switch to asynchronous publish mode to keep the hot path non-blocking.
3. Increase socket buffer sizes (`send_socket_buffer_size`/`recv_socket_buffer_size`) to avoid OS-level packet drops under burst load.
4. Use `KEEP_LAST` history with a tuned depth instead of `KEEP_ALL` to bound memory and avoid unbounded queue growth (see topic 9).
5. Profile with vendor tools (e.g., RTI Monitor, Cyclone DDS tracing) to identify whether the bottleneck is CPU (serialization), network (bandwidth), or OS (socket buffers).

## Common Pitfalls
- Enabling batching for latency-critical control-loop topics — the added `max_flush_delay` can silently violate real-time deadlines.
- Leaving default socket buffer sizes in high-throughput deployments, causing invisible packet loss that RELIABLE QoS then "fixes" via costly retransmission storms.
- Over-provisioning thread pools beyond available CPU cores, causing contention instead of parallelism gains.
- Forgetting that async publish mode changes write() semantics (return before actually sent) — code that assumes synchronous delivery can behave unexpectedly.

## Key Takeaways
- Batching trades a small, bounded added latency for large gains in throughput and reduced per-packet overhead — tune `max_flush_delay` to your latency budget.
- Asynchronous publish mode decouples application-thread latency from network send latency.
- Thread pool sizing should match available CPU cores and workload characteristics, not be maximized blindly.
- Flow controllers protect against writers overwhelming slower consumers or shared links.
- Always profile before tuning — CPU, network, and OS-level bottlenecks each call for different fixes.
