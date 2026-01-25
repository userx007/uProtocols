# CAN Message Scheduling Strategies

## Overview

CAN (Controller Area Network) message scheduling is critical for deterministic real-time communication in automotive and industrial systems. The scheduling strategy determines **when** messages are transmitted on the bus, directly impacting system predictability, latency, and bandwidth utilization.

## Core Scheduling Approaches

### 1. Time-Triggered Scheduling

Time-triggered systems transmit messages at predefined, periodic intervals. This approach provides **deterministic behavior** where message transmission times are known in advance.

**Characteristics:**
- Messages sent at fixed time intervals (e.g., every 10ms, 100ms)
- Predictable bus load and latency
- Simplified worst-case execution time (WCET) analysis
- May waste bandwidth when data hasn't changed
- Ideal for cyclic sensor data and control loops

**Use Cases:**
- Engine control unit (ECU) sensor readings
- Periodic status messages
- Time-synchronized control systems
- Safety-critical applications requiring predictability

### 2. Event-Triggered Scheduling

Event-triggered systems transmit messages only when specific events occur or when data changes significantly.

**Characteristics:**
- Messages sent in response to events (button press, threshold crossing, state change)
- Efficient bandwidth utilization
- Variable latency depending on bus load
- Requires careful arbitration design
- More complex timing analysis

**Use Cases:**
- User input events (buttons, switches)
- Alarm/fault conditions
- Sporadic diagnostics requests
- Change-of-state notifications

### 3. Hybrid Scheduling

Most real-world systems combine both approaches, using time-triggered for periodic data and event-triggered for sporadic events.

## Priority-Based Scheduling

CAN uses **identifier-based arbitration** where lower identifier values have higher priority. This creates a natural priority scheme:

```
Priority:  HIGH ←--------------------------------→ LOW
CAN ID:    0x000                                 0x7FF (11-bit)
           0x00000000                            0x1FFFFFFF (29-bit)
```

**Priority Assignment Strategies:**

1. **Rate Monotonic** - Higher frequency messages get higher priority
2. **Deadline Monotonic** - Shorter deadline messages get higher priority  
3. **Safety-Critical First** - Safety messages get highest priority regardless of frequency
4. **Mixed Criticality** - Partition ID space by criticality levels

## Code Examples

### C/C++ Implementation

[CAN_Message_Scheduling.cpp](../src/17/CAN_Message_Scheduling.cpp)<br>

### Rust Implementation

[CAN_Message_Scheduling.rs](../src/17/CAN_Message_Scheduling.rs)<br>

## Advanced Scheduling Concepts

### Rate Monotonic Analysis (RMA)

For time-triggered systems, Rate Monotonic Analysis ensures schedulability:

**Schedulability Test:**
```
U = Σ(Ci / Ti) ≤ n(2^(1/n) - 1)

Where:
- Ci = Worst-case execution time of task i
- Ti = Period of task i
- n = Number of tasks
- For n→∞, limit ≈ 0.693 (69.3% utilization)
```

**Priority Assignment:** Higher frequency (shorter period) → Higher priority (lower CAN ID)

### Deadline Monitoring

Both scheduling approaches should monitor message deadlines to detect timing violations:

```c
bool check_deadline_miss(schedule_entry_t* entry, uint32_t current_time) {
    uint32_t elapsed = current_time - entry->msg.timestamp;
    if (elapsed > entry->deadline_ms) {
        // Log deadline violation
        log_error("Message 0x%03X missed deadline", entry->msg.id);
        return true;
    }
    return false;
}
```

### Bus Load Management

Calculate theoretical bus load to ensure schedulability:

```
Bus Load = Σ(Message_Bitsi × Frequencyi) / Bus_Bandwidth

For 500 kbit/s CAN:
Message with DLC=8 at 100Hz:
(111 bits × 100) / 500000 = 2.22% load
```

**Design Goal:** Keep total bus load under 70% for robust operation.

## Practical Scheduling Patterns

### Pattern 1: Layered Priority Architecture

```
0x000-0x0FF: Safety-critical (airbag, brakes)     - Event-triggered
0x100-0x1FF: Powertrain control (engine, trans)   - Time-triggered 10-20ms
0x200-0x2FF: Chassis systems (ABS, steering)      - Time-triggered 20-50ms
0x300-0x3FF: Body control (lights, windows)       - Hybrid 100-500ms
0x400-0x7FF: Diagnostics and infotainment         - Event-triggered
```

### Pattern 2: Synchronized Phase Offset

To avoid simultaneous transmissions, offset periodic messages:

```c
// Message A: Period 10ms, Offset 0ms
// Message B: Period 10ms, Offset 3ms
// Message C: Period 10ms, Offset 6ms

if ((current_time % 10) == 0) send_message_A();
if ((current_time % 10) == 3) send_message_B();
if ((current_time % 10) == 6) send_message_C();
```

### Pattern 3: Adaptive Scheduling

Dynamically adjust transmission rates based on system state:

```rust
fn adaptive_period(current_speed: u16) -> Duration {
    match current_speed {
        0..=30 => Duration::from_millis(100),      // Low speed: slower updates
        31..=80 => Duration::from_millis(50),      // Medium speed
        _ => Duration::from_millis(20),            // High speed: faster updates
    }
}
```

## Summary

**CAN Message Scheduling Strategies** determine when messages are transmitted on the bus, directly impacting system determinism, latency, and reliability.

**Key Scheduling Approaches:**
- **Time-Triggered:** Periodic transmission at fixed intervals providing predictable, deterministic behavior ideal for control loops and sensor data
- **Event-Triggered:** On-demand transmission responding to events, optimizing bandwidth but with variable latency
- **Hybrid:** Combines both approaches for practical systems balancing determinism and efficiency

**Priority-Based Arbitration:** CAN's identifier-based arbitration creates a natural priority system where lower IDs win bus access. Priority assignment strategies include rate monotonic (higher frequency = higher priority), deadline monotonic (shorter deadline = higher priority), and safety-first approaches.

**Implementation Considerations:**
- Use priority queues to manage transmission order
- Monitor deadlines to detect timing violations  
- Calculate bus load to ensure schedulability (target <70%)
- Apply phase offsets to periodic messages to reduce collisions
- Consider adaptive scheduling that adjusts rates based on system state

**Design Tradeoffs:** Time-triggered scheduling offers predictability and simpler analysis but may waste bandwidth. Event-triggered scheduling optimizes bandwidth but requires careful worst-case analysis. Most automotive systems use hybrid approaches, with high-priority safety and control messages time-triggered at fast rates, and lower-priority status/diagnostic messages event-triggered.

The choice of scheduling strategy fundamentally shapes the real-time behavior and reliability of CAN-based embedded systems.