# 69. UART Routing and Switching

**Core Concepts** — the routing matrix abstraction, five switching topologies (unicast through crossbar), and the different triggers that can drive route changes at runtime.

**Hardware Foundations** — analog switch ICs like the 74HC4052, FPGA crossbars, and software-only MCU approaches.

**C/C++ Implementation** — ring buffer structs, a bitmask routing table, a polling router task, ISR RX callbacks (STM32 HAL style), a control command parser, and a framing-based `FrameRouter` class that inspects packet headers to route entire frames dynamically.

**Rust Implementation** — `heapless` SPSC queues for ISR-safe data transfer, a compact bitmask `RoutingTable`, a generic `router_tick` function, an RTIC 2.x ISR example, a `FrameRouter` using a const-generic payload buffer, and a `parse_command` function for runtime reconfiguration.

**Advanced Patterns** — TDM for half-duplex buses, priority-based routing, non-intrusive tap/snooping routes, and drain-then-switch for safe hot route changes.

**Summary** — a concise recap plus a trade-off table comparing polling vs. ISR router approaches.

### Dynamic Routing of UART Streams Between Devices

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Hardware Foundations](#hardware-foundations)
4. [Software Routing Architectures](#software-routing-architectures)
5. [C/C++ Implementation](#cc-implementation)
6. [Rust Implementation](#rust-implementation)
7. [Advanced Patterns](#advanced-patterns)
8. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) routing and switching refers to the dynamic redirection of serial data streams between multiple source and destination devices at runtime — without rewiring hardware. In embedded systems, this capability is essential for scenarios such as:

- **Debug multiplexing**: routing console output to USB, Bluetooth, or a physical header
- **Protocol bridging**: forwarding data between a GSM modem, GPS module, and a host MCU
- **Multi-tenant firmware**: switching control between a bootloader UART, application UART, and OTA update port
- **Test harnesses**: injecting or intercepting UART traffic programmatically

UART routing can be implemented in hardware (using analog switches, crossbar ICs, or FPGA fabric) or in software (ring buffers, DMA ping-pong, interrupt-driven dispatch tables). Most real-world solutions combine both.

---

## Core Concepts

### 1. The Routing Matrix

A routing matrix abstracts the connection topology. Each entry `R[src][dst]` indicates whether data received on source `src` should be forwarded to destination `dst`. A single source can fan-out to multiple destinations (broadcast), and multiple sources can be merged into one destination (mux/aggregate).

```
         DST0   DST1   DST2
SRC0  [  ON     OFF    ON  ]   -> SRC0 fans out to DST0 and DST2
SRC1  [  OFF    ON     OFF ]   -> SRC1 routes only to DST1
SRC2  [  ON     ON     OFF ]   -> SRC2 fans out to DST0 and DST1
```

### 2. Routing Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| **Unicast** | 1 source → 1 destination | Simple pass-through bridge |
| **Broadcast** | 1 source → N destinations | Debug mirroring, logging |
| **Multicast** | 1 source → selected subset | Protocol fan-out |
| **Mux** | N sources → 1 destination | Console aggregation |
| **Crossbar** | Any source → any destination | Full flexible switching |

### 3. Switching Triggers

Routing decisions can be triggered by:

- **Static configuration** at boot (compile-time or via config struct)
- **Received framing bytes** (e.g., a magic header selects the target port)
- **GPIO signals** or DIP switches
- **Control commands** over a dedicated management UART
- **Timer-based round-robin** (time-division multiplexing)
- **RTOS events / semaphores** (task-driven switching)

---

## Hardware Foundations

### Analog Switch ICs

Devices like the **74HC4052** (dual 4-channel analog mux) or **TS3USB30** allow physical TX/RX line switching under GPIO control. The MCU drives select lines to connect different peripherals.

```
MCU TX ──┬── [74HC4052] ──── GPS TX
         │         │
         │    SEL[1:0]       GSM TX
         │    (GPIO)         BLE TX
         └──                 HOST TX
```

Latency is sub-microsecond, but only one route is active at a time.

### FPGA / CPLD Crossbars

For high-speed or simultaneous multi-path routing, an FPGA fabric implements a full crossbar switch where every input can be independently routed to every output with no contention. Configuration is done via SPI or parallel bus from the MCU.

### Software-Only Routing (MCU)

When hardware switching is unavailable, the MCU's interrupt service routines (ISRs) or DMA transfer complete callbacks copy bytes between port ring buffers, implementing the routing matrix entirely in firmware.

---

## Software Routing Architectures

### Architecture A: ISR-Driven Direct Dispatch

Each UART RX ISR directly forwards the received byte to the target port(s) TX buffer based on the current routing table. This minimizes latency but increases ISR complexity.

```
[UART0 RX ISR] → lookup route_table[0] → push to UART1.tx_buf
                                        → push to UART2.tx_buf  (broadcast)
```

### Architecture B: Ring Buffer + Router Task

Each UART feeds its received data into a dedicated ring buffer. A central router task (or main loop) drains each source buffer and dispatches bytes to destination buffers according to the routing matrix. This decouples timing but adds latency proportional to scheduler jitter.

```
UART0 RX → [ring_buf_0] ──┐
UART1 RX → [ring_buf_1] ──┤─ Router Task ─┬→ UART0 TX ← [ring_buf_tx0]
UART2 RX → [ring_buf_2] ──┘               ├→ UART1 TX ← [ring_buf_tx1]
                                          └→ UART2 TX ← [ring_buf_tx2]
```

### Architecture C: DMA Ping-Pong with Scatter-Gather

Advanced MCUs (STM32, NXP i.MX RT) support DMA linked-list descriptors. The DMA engine itself can scatter received data to multiple destination buffers in a single transfer, without CPU involvement.

---

## C/C++ Implementation

### 1. Data Structures

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define UART_PORT_COUNT   4
#define RING_BUF_SIZE     256

/* ---------- Ring Buffer ---------- */
typedef struct {
    uint8_t  buf[RING_BUF_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} RingBuffer;

static inline bool rb_push(RingBuffer *rb, uint8_t byte) {
    if (rb->count >= RING_BUF_SIZE) return false;
    rb->buf[rb->head] = byte;
    rb->head = (rb->head + 1) % RING_BUF_SIZE;
    rb->count++;
    return true;
}

static inline bool rb_pop(RingBuffer *rb, uint8_t *byte) {
    if (rb->count == 0) return false;
    *byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUF_SIZE;
    rb->count--;
    return true;
}

/* ---------- UART Port Abstraction ---------- */
typedef struct {
    uint8_t      port_id;
    RingBuffer   rx_buf;
    RingBuffer   tx_buf;
    bool         enabled;
    /* HAL handle — platform-specific */
    void        *hal_handle;
} UartPort;

/* ---------- Routing Table ---------- */
typedef struct {
    /* route_matrix[src][dst] = true means SRC → DST is active */
    bool route_matrix[UART_PORT_COUNT][UART_PORT_COUNT];
} RoutingTable;
```

### 2. Routing Table Operations

```c
/* Global state */
static UartPort     g_ports[UART_PORT_COUNT];
static RoutingTable g_routing;

/* Initialize: all routes OFF */
void routing_init(void) {
    memset(&g_routing, 0, sizeof(g_routing));
}

/* Enable a route: data from 'src' will be forwarded to 'dst' */
void routing_set(uint8_t src, uint8_t dst, bool enabled) {
    if (src < UART_PORT_COUNT && dst < UART_PORT_COUNT)
        g_routing.route_matrix[src][dst] = enabled;
}

/* Query a route */
bool routing_get(uint8_t src, uint8_t dst) {
    return g_routing.route_matrix[src][dst];
}

/* Convenience: connect a single src→dst, clearing all other dst routes for src */
void routing_unicast(uint8_t src, uint8_t dst) {
    for (uint8_t d = 0; d < UART_PORT_COUNT; d++)
        g_routing.route_matrix[src][d] = (d == dst);
}

/* Broadcast: src → ALL enabled ports */
void routing_broadcast(uint8_t src) {
    for (uint8_t d = 0; d < UART_PORT_COUNT; d++)
        g_routing.route_matrix[src][d] = g_ports[d].enabled;
}
```

### 3. Router Task (Polling Style)

```c
/*
 * Called periodically (main loop or RTOS task).
 * Drains each port's RX buffer and forwards bytes
 * to all routed destination TX buffers.
 */
void uart_router_task(void) {
    for (uint8_t src = 0; src < UART_PORT_COUNT; src++) {
        if (!g_ports[src].enabled) continue;

        uint8_t byte;
        while (rb_pop(&g_ports[src].rx_buf, &byte)) {
            for (uint8_t dst = 0; dst < UART_PORT_COUNT; dst++) {
                if (dst == src) continue;   /* no loopback */
                if (!g_ports[dst].enabled) continue;
                if (!g_routing.route_matrix[src][dst]) continue;

                if (!rb_push(&g_ports[dst].tx_buf, byte)) {
                    /* TX buffer full — handle overflow (drop, block, or log) */
                }
            }
        }
    }
}
```

### 4. ISR-Driven RX Feed

```c
/*
 * Called from UART RX interrupt (or HAL callback).
 * Platform-specific; example uses STM32 HAL convention.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    for (uint8_t i = 0; i < UART_PORT_COUNT; i++) {
        if (g_ports[i].hal_handle == huart) {
            /* 'last_rx_byte' is a per-port staging byte for DMA/IT mode */
            rb_push(&g_ports[i].rx_buf, g_ports[i].last_rx_byte);
            /* Re-arm single-byte reception */
            HAL_UART_Receive_IT(huart, &g_ports[i].last_rx_byte, 1);
            break;
        }
    }
}
```

### 5. Dynamic Route Switching via Control Commands

```c
/*
 * Simple text-command parser on a management port.
 * Commands:  "ROUTE src dst 1"  or  "ROUTE src dst 0"
 * Example:   "ROUTE 0 2 1"  ->  enable route from UART0 to UART2
 */
void parse_control_command(const char *cmd) {
    uint8_t src, dst, enable;
    if (sscanf(cmd, "ROUTE %hhu %hhu %hhu", &src, &dst, &enable) == 3) {
        routing_set(src, dst, (bool)enable);
    } else if (strncmp(cmd, "BCAST ", 6) == 0) {
        uint8_t port;
        if (sscanf(cmd + 6, "%hhu", &port) == 1)
            routing_broadcast(port);
    }
}
```

### 6. Framing-Based Automatic Switching (C++)

```cpp
#include <cstdint>
#include <array>
#include <functional>

/*
 * Frame format:  [0xAA] [DST_PORT] [LEN] [PAYLOAD...]
 * The router inspects each frame header and routes the
 * payload to the specified destination port.
 */
class FrameRouter {
public:
    static constexpr uint8_t  MAGIC      = 0xAA;
    static constexpr uint8_t  MAX_PORTS  = 4;
    static constexpr uint16_t MAX_FRAME  = 128;

    using TxCallback = std::function<void(uint8_t port, const uint8_t*, uint16_t)>;

    explicit FrameRouter(TxCallback tx_cb) : tx_cb_(tx_cb) {}

    /* Feed one byte from any source (src_port identifies origin) */
    void feed(uint8_t src_port, uint8_t byte) {
        (void)src_port;          /* could log origin for debugging */
        switch (state_) {
            case State::WAIT_MAGIC:
                if (byte == MAGIC) state_ = State::WAIT_DST;
                break;
            case State::WAIT_DST:
                dst_port_ = byte;
                state_    = (byte < MAX_PORTS) ? State::WAIT_LEN : State::WAIT_MAGIC;
                break;
            case State::WAIT_LEN:
                payload_len_ = byte;
                payload_idx_ = 0;
                state_       = (byte > 0) ? State::RECV_PAYLOAD : State::WAIT_MAGIC;
                break;
            case State::RECV_PAYLOAD:
                if (payload_idx_ < MAX_FRAME)
                    frame_buf_[payload_idx_++] = byte;
                if (payload_idx_ >= payload_len_) {
                    tx_cb_(dst_port_, frame_buf_.data(), payload_len_);
                    state_ = State::WAIT_MAGIC;
                }
                break;
        }
    }

private:
    enum class State { WAIT_MAGIC, WAIT_DST, WAIT_LEN, RECV_PAYLOAD };
    State                          state_       = State::WAIT_MAGIC;
    uint8_t                        dst_port_    = 0;
    uint8_t                        payload_len_ = 0;
    uint8_t                        payload_idx_ = 0;
    std::array<uint8_t, MAX_FRAME> frame_buf_   = {};
    TxCallback                     tx_cb_;
};
```

---

## Rust Implementation

Rust's ownership model and `no_std` ecosystem make it particularly well-suited for safe, concurrent UART routing firmware.

### 1. Ring Buffer (heapless, no_std)

```rust
// Cargo.toml dependency: heapless = "0.8"
use heapless::spsc::{Consumer, Producer, Queue};

const RING_CAPACITY: usize = 256;

// Each UART port gets a statically allocated SPSC queue.
// Producer is owned by the ISR; Consumer by the router task.
static mut UART0_QUEUE: Queue<u8, RING_CAPACITY> = Queue::new();
static mut UART1_QUEUE: Queue<u8, RING_CAPACITY> = Queue::new();
static mut UART2_QUEUE: Queue<u8, RING_CAPACITY> = Queue::new();
```

### 2. Routing Table

```rust
pub const PORT_COUNT: usize = 4;

/// Bitmask-based routing table: route_mask[src] is a bitmask of
/// destination ports. Bit N set means "forward to port N".
#[derive(Clone, Copy, Debug)]
pub struct RoutingTable {
    pub route_mask: [u8; PORT_COUNT],
}

impl RoutingTable {
    pub const fn new() -> Self {
        Self { route_mask: [0u8; PORT_COUNT] }
    }

    /// Enable or disable the route src → dst.
    pub fn set(&mut self, src: usize, dst: usize, enabled: bool) {
        assert!(src < PORT_COUNT && dst < PORT_COUNT);
        if enabled {
            self.route_mask[src] |= 1 << dst;
        } else {
            self.route_mask[src] &= !(1 << dst);
        }
    }

    /// Route src → exactly one dst (unicast).
    pub fn unicast(&mut self, src: usize, dst: usize) {
        assert!(src < PORT_COUNT && dst < PORT_COUNT);
        self.route_mask[src] = 1 << dst;
    }

    /// Route src → all ports (broadcast).
    pub fn broadcast(&mut self, src: usize) {
        assert!(src < PORT_COUNT);
        self.route_mask[src] = (1 << PORT_COUNT) - 1;
        self.route_mask[src] &= !(1 << src); // no self-loopback
    }

    /// Returns an iterator over destination port indices for a given source.
    pub fn destinations(&self, src: usize) -> impl Iterator<Item = usize> + '_ {
        let mask = self.route_mask[src];
        (0..PORT_COUNT).filter(move |&dst| mask & (1 << dst) != 0)
    }
}
```

### 3. Router Core

```rust
use core::cell::RefCell;
use cortex_m::interrupt::Mutex;

/// Shared mutable routing table protected by a critical-section mutex.
static ROUTING: Mutex<RefCell<RoutingTable>> =
    Mutex::new(RefCell::new(RoutingTable::new()));

/// Trait for abstracting UART TX.
pub trait UartTx {
    fn send_byte(&mut self, byte: u8) -> Result<(), ()>;
}

/// One iteration of the router: drain each source queue,
/// forward bytes to all routed destinations.
///
/// `sources`  — array of byte iterators (one per RX queue)
/// `sinks`    — array of TX drivers (one per port)
pub fn router_tick<Src, Snk>(
    sources: &mut [Option<Src>; PORT_COUNT],
    sinks:   &mut [Option<Snk>; PORT_COUNT],
)
where
    Src: Iterator<Item = u8>,
    Snk: UartTx,
{
    // Snapshot the routing table inside a critical section.
    let table = cortex_m::interrupt::free(|cs| {
        *ROUTING.borrow(cs).borrow()
    });

    for src in 0..PORT_COUNT {
        let Some(ref mut source) = sources[src] else { continue };

        while let Some(byte) = source.next() {
            for dst in table.destinations(src) {
                if let Some(ref mut sink) = sinks[dst] {
                    let _ = sink.send_byte(byte);
                }
            }
        }
    }
}
```

### 4. ISR-Side Producer (RTIC / bare-metal)

```rust
// Example using RTIC 2.x syntax (rtic = "2")
#[rtic::app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0])]
mod app {
    use heapless::spsc::Producer;
    use super::RING_CAPACITY;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        uart0_prod: Producer<'static, u8, RING_CAPACITY>,
        uart0_rx:   stm32f4xx_hal::serial::Rx<stm32f4xx_hal::pac::USART1>,
    }

    /// UART0 RX interrupt: push received byte into the queue.
    #[task(binds = USART1, local = [uart0_prod, uart0_rx])]
    fn usart1_isr(cx: usart1_isr::Context) {
        if let Ok(byte) = cx.local.uart0_rx.read() {
            // Drop on overflow — producer is non-blocking.
            let _ = cx.local.uart0_prod.enqueue(byte);
        }
    }
}
```

### 5. Framing-Based Router in Rust

```rust
/// Frame format: [0xAA] [DST_PORT:u8] [LEN:u8] [PAYLOAD...]
pub struct FrameRouter<Snk: UartTx, const N: usize> {
    state:   FrameState,
    dst:     usize,
    len:     usize,
    idx:     usize,
    payload: [u8; N],
    sinks:   [Option<Snk>; PORT_COUNT],
}

#[derive(PartialEq)]
enum FrameState { WaitMagic, WaitDst, WaitLen, RecvPayload }

impl<Snk: UartTx, const N: usize> FrameRouter<Snk, N> {
    const MAGIC: u8 = 0xAA;

    pub fn feed(&mut self, byte: u8) {
        match self.state {
            FrameState::WaitMagic => {
                if byte == Self::MAGIC { self.state = FrameState::WaitDst; }
            }
            FrameState::WaitDst => {
                if (byte as usize) < PORT_COUNT {
                    self.dst   = byte as usize;
                    self.state = FrameState::WaitLen;
                } else {
                    self.state = FrameState::WaitMagic;
                }
            }
            FrameState::WaitLen => {
                self.len   = byte as usize;
                self.idx   = 0;
                self.state = if self.len > 0 {
                    FrameState::RecvPayload
                } else {
                    FrameState::WaitMagic
                };
            }
            FrameState::RecvPayload => {
                if self.idx < N {
                    self.payload[self.idx] = byte;
                    self.idx += 1;
                }
                if self.idx >= self.len {
                    // Dispatch complete frame to destination port.
                    if let Some(ref mut sink) = self.sinks[self.dst] {
                        for &b in &self.payload[..self.len] {
                            let _ = sink.send_byte(b);
                        }
                    }
                    self.state = FrameState::WaitMagic;
                }
            }
        }
    }
}
```

### 6. Runtime Route Switching via Command Parser

```rust
/// Parse a simple ASCII command and update the routing table.
/// Format: "ROUTE <src> <dst> <0|1>\n"  or  "BCAST <src>\n"
pub fn parse_command(cmd: &str) {
    let parts: heapless::Vec<&str, 8> =
        cmd.trim().split_whitespace().collect();

    match parts.as_slice() {
        ["ROUTE", src, dst, flag] => {
            if let (Ok(s), Ok(d), Ok(f)) = (
                src.parse::<usize>(),
                dst.parse::<usize>(),
                flag.parse::<u8>(),
            ) {
                cortex_m::interrupt::free(|cs| {
                    ROUTING.borrow(cs).borrow_mut().set(s, d, f != 0);
                });
            }
        }
        ["BCAST", src] => {
            if let Ok(s) = src.parse::<usize>() {
                cortex_m::interrupt::free(|cs| {
                    ROUTING.borrow(cs).borrow_mut().broadcast(s);
                });
            }
        }
        _ => { /* Unknown command — ignore or log */ }
    }
}
```

---

## Advanced Patterns

### Time-Division Multiplexing (TDM)

When two devices share one physical UART (e.g., a half-duplex RS-485 bus), TDM allocates fixed time slots to each source:

```c
/* Simple 2-device TDM, switched every 10 ms via SysTick */
void SysTick_Handler(void) {
    static uint8_t slot = 0;
    slot ^= 1;                            /* toggle between 0 and 1 */
    routing_unicast(slot, SHARED_BUS);    /* only one source active */
}
```

### Priority-Based Routing

Assign priority levels to source ports. Higher-priority data preempts lower-priority data in the destination TX queue:

```c
typedef struct {
    uint8_t byte;
    uint8_t priority;  /* lower value = higher priority */
} PrioEntry;

/* Use a min-heap priority queue as the TX buffer instead of FIFO */
```

### Snooping / Tap

A tap route duplicates traffic to a logging/debug port without affecting normal routing. It is implemented as a broadcast route that includes the debug port:

```c
void routing_add_tap(uint8_t src, uint8_t tap_port) {
    g_routing.route_matrix[src][tap_port] = true;
    /* Does NOT clear existing routes — purely additive */
}
```

### Hot-Swap Safe Switching

When switching routes at runtime, in-flight data in the ring buffers must be flushed before the route change to avoid data interleaving. Use a drain-then-switch pattern:

```c
void routing_safe_switch(uint8_t src, uint8_t new_dst) {
    /* 1. Drain existing TX buffers */
    flush_port_tx(src);
    /* 2. Atomic route update */
    __disable_irq();
    routing_unicast(src, new_dst);
    __enable_irq();
}
```

---

## Summary

UART routing and switching enables dynamic, software-configurable serial connectivity in embedded systems. Key takeaways:

**Architecture**: A routing matrix (dense boolean array or bitmask per source) is the central data structure. It maps each source UART port to one or more destination ports, supporting unicast, broadcast, multicast, and crossbar topologies.

**Hardware assistance**: Analog switch ICs (74HC405x series) provide sub-microsecond physical line switching under GPIO control. FPGAs offer full non-blocking crossbars for high-performance scenarios.

**C/C++ patterns**: ISR-driven ring buffers feed a central router task that consults the routing matrix and copies bytes to destination TX buffers. Frame-based routers extend this by inspecting packet headers to make per-frame routing decisions at runtime.

**Rust patterns**: The `heapless` SPSC queue enables safe ISR-to-task data transfer without dynamic allocation. Bitmask routing tables are compact and efficient. Critical-section mutexes (`cortex_m::interrupt::free`) protect shared routing state. RTIC provides a structured interrupt/task model for concurrent port management.

**Advanced patterns**: TDM handles half-duplex bus sharing; priority queues ensure critical traffic is not starved; tap routes provide non-intrusive traffic inspection; drain-then-switch ensures data integrity during hot route changes.

**Trade-offs**:

| Concern | Polling Router | ISR Router |
|---------|----------------|------------|
| Latency | Higher (task period) | Minimal (byte-level) |
| ISR complexity | Low | Higher |
| Throughput | Batch efficient | Per-byte overhead |
| Overflow risk | Larger buffers needed | Smaller windows |

UART routing is a building block for protocol bridges, serial multiplexers, debug infrastructure, and any embedded application requiring flexible, runtime-reconfigurable communication topologies.

---

*Document: 69_UART_Routing_And_Switching.md — Part of the Embedded UART Programming Series*