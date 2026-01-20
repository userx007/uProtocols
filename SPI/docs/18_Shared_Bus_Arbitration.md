# 18. Shared Bus Arbitration

## Overview

Shared Bus Arbitration in SPI refers to the coordination mechanisms required when multiple master devices need to control the same SPI bus. While SPI is fundamentally designed as a single-master protocol, certain applications require multiple masters to share the same bus, necessitating arbitration schemes to prevent conflicts and ensure data integrity.

Unlike I2C, which has built-in multi-master arbitration, SPI has no native protocol-level support for this scenario, making it a challenging but occasionally necessary configuration in complex embedded systems.

## Why Shared Bus Arbitration is Rare in SPI

**Fundamental Design Differences:**
- SPI uses separate chip select lines for each slave, but has no mechanism for master collision detection
- The protocol operates at high speeds where bus contention can cause immediate corruption
- No built-in acknowledgment system exists to detect conflicts
- Clock and data lines are driven strongly, making electrical conflicts problematic

**When Multiple Masters Are Needed:**
- Redundant systems requiring failover capability
- Multi-processor systems sharing peripheral devices
- Development/debugging scenarios with multiple controllers
- Systems requiring dynamic master role switching

## Arbitration Strategies

### 1. **Hardware-Based Arbitration**

**External Arbiter Circuit:**
The most reliable approach uses dedicated hardware to manage bus access:

```
┌─────────┐         ┌──────────────┐         ┌────────┐
│Master 1 ├────────►│              │         │        │
└─────────┘   REQ1  │   Arbiter    ├────────►│  Mux   │──► SPI Bus
┌─────────┐         │   Circuit    │  Grant  │        │
│Master 2 ├────────►│              ├────────►│        │
└─────────┘   REQ2  └──────────────┘         └────────┘
```

**Key Components:**
- Request lines from each master
- Grant signals controlling tri-state buffers or multiplexers
- Priority logic (fixed, round-robin, or weighted)
- Bus-busy detection

### 2. **Software-Based Arbitration**

Uses a shared semaphore or flag in memory, typically with additional handshaking signals.

### 3. **Time-Division Multiple Access (TDMA)**

Masters take turns accessing the bus in predetermined time slots.

## C/C++ Implementation Examples

### Hardware Arbitration with Request/Grant Protocol

```c
#include <stdint.h>
#include <stdbool.h>

// GPIO pins for arbitration
#define ARB_REQUEST_PIN  GPIO_PIN_5
#define ARB_GRANT_PIN    GPIO_PIN_6
#define ARB_TIMEOUT_MS   100

typedef enum {
    ARB_STATE_IDLE,
    ARB_STATE_REQUESTING,
    ARB_STATE_GRANTED,
    ARB_STATE_TIMEOUT
} arbitration_state_t;

typedef struct {
    arbitration_state_t state;
    uint32_t request_time;
    bool is_master_active;
} spi_arbiter_t;

static spi_arbiter_t arbiter = {
    .state = ARB_STATE_IDLE,
    .request_time = 0,
    .is_master_active = false
};

// Request bus access
bool spi_arbitration_request(uint32_t timeout_ms) {
    uint32_t start_time = get_system_tick();
    
    arbiter.state = ARB_STATE_REQUESTING;
    arbiter.request_time = start_time;
    
    // Assert request line
    gpio_set_pin(ARB_REQUEST_PIN);
    
    // Wait for grant signal
    while (!gpio_read_pin(ARB_GRANT_PIN)) {
        if ((get_system_tick() - start_time) > timeout_ms) {
            arbiter.state = ARB_STATE_TIMEOUT;
            gpio_clear_pin(ARB_REQUEST_PIN);
            return false;
        }
        
        // Small delay to prevent busy waiting
        delay_us(10);
    }
    
    arbiter.state = ARB_STATE_GRANTED;
    arbiter.is_master_active = true;
    
    return true;
}

// Release bus access
void spi_arbitration_release(void) {
    if (arbiter.state == ARB_STATE_GRANTED) {
        // De-assert request line
        gpio_clear_pin(ARB_REQUEST_PIN);
        
        // Wait briefly for arbiter to acknowledge
        delay_us(5);
        
        arbiter.state = ARB_STATE_IDLE;
        arbiter.is_master_active = false;
    }
}

// SPI transaction with arbitration
int spi_arbitrated_transfer(uint8_t *tx_data, uint8_t *rx_data, 
                            size_t length, uint8_t cs_pin) {
    // Request bus access
    if (!spi_arbitration_request(ARB_TIMEOUT_MS)) {
        return -1; // Arbitration failed
    }
    
    // Perform SPI transaction
    gpio_clear_pin(cs_pin);  // Assert CS
    
    for (size_t i = 0; i < length; i++) {
        rx_data[i] = spi_transfer_byte(tx_data[i]);
    }
    
    gpio_set_pin(cs_pin);    // De-assert CS
    
    // Release bus
    spi_arbitration_release();
    
    return 0; // Success
}
```

### Software Semaphore-Based Arbitration

```cpp
#include <atomic>
#include <chrono>
#include <cstdint>

class SPIBusArbiter {
private:
    std::atomic<uint8_t> bus_owner{0xFF};  // 0xFF = no owner
    std::atomic<bool> bus_locked{false};
    uint8_t master_id;
    
    static constexpr uint32_t MAX_RETRY = 1000;
    static constexpr uint32_t RETRY_DELAY_US = 50;

public:
    SPIBusArbiter(uint8_t id) : master_id(id) {}
    
    // Try to acquire bus with timeout
    bool acquire(uint32_t timeout_ms = 100) {
        auto start = std::chrono::steady_clock::now();
        uint32_t retries = 0;
        
        while (retries < MAX_RETRY) {
            // Try to atomically claim the bus
            uint8_t expected = 0xFF;
            if (bus_owner.compare_exchange_strong(expected, master_id)) {
                // Successfully claimed
                bus_locked.store(true, std::memory_order_release);
                return true;
            }
            
            // Check timeout
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            
            if (elapsed >= timeout_ms) {
                return false;
            }
            
            // Brief delay before retry
            delay_us(RETRY_DELAY_US);
            retries++;
        }
        
        return false;
    }
    
    // Release bus ownership
    void release() {
        if (bus_owner.load() == master_id) {
            bus_locked.store(false, std::memory_order_release);
            bus_owner.store(0xFF, std::memory_order_release);
        }
    }
    
    // Check if we own the bus
    bool owns_bus() const {
        return bus_owner.load() == master_id;
    }
    
    // RAII wrapper for automatic release
    class BusLock {
    private:
        SPIBusArbiter& arbiter;
        bool locked;
        
    public:
        BusLock(SPIBusArbiter& arb, uint32_t timeout_ms = 100) 
            : arbiter(arb), locked(arb.acquire(timeout_ms)) {}
        
        ~BusLock() {
            if (locked) {
                arbiter.release();
            }
        }
        
        bool is_locked() const { return locked; }
        
        // Prevent copying
        BusLock(const BusLock&) = delete;
        BusLock& operator=(const BusLock&) = delete;
    };
};

// Usage example
SPIBusArbiter arbiter(1);  // Master ID = 1

int spi_safe_transaction(uint8_t* tx_buf, uint8_t* rx_buf, size_t len) {
    SPIBusArbiter::BusLock lock(arbiter, 200);  // 200ms timeout
    
    if (!lock.is_locked()) {
        return -1;  // Failed to acquire bus
    }
    
    // Perform SPI transaction - bus automatically released when lock goes out of scope
    return spi_transfer(tx_buf, rx_buf, len);
}
```

### Priority-Based Arbitration

```c
#define MAX_MASTERS 4

typedef struct {
    uint8_t master_id;
    uint8_t priority;      // Lower number = higher priority
    bool requesting;
    uint32_t request_time;
} master_info_t;

typedef struct {
    master_info_t masters[MAX_MASTERS];
    uint8_t current_owner;
    bool bus_busy;
} priority_arbiter_t;

static priority_arbiter_t arb = {
    .current_owner = 0xFF,
    .bus_busy = false
};

// Initialize master information
void arbiter_init(void) {
    for (int i = 0; i < MAX_MASTERS; i++) {
        arb.masters[i].master_id = i;
        arb.masters[i].priority = i;  // Default priorities
        arb.masters[i].requesting = false;
        arb.masters[i].request_time = 0;
    }
}

// Find highest priority requester
static int find_highest_priority(void) {
    int highest_idx = -1;
    uint8_t highest_priority = 0xFF;
    
    for (int i = 0; i < MAX_MASTERS; i++) {
        if (arb.masters[i].requesting && 
            arb.masters[i].priority < highest_priority) {
            highest_priority = arb.masters[i].priority;
            highest_idx = i;
        }
    }
    
    return highest_idx;
}

// Request bus (called by master)
bool arbiter_request_bus(uint8_t master_id, uint32_t timeout_ms) {
    if (master_id >= MAX_MASTERS) return false;
    
    uint32_t start = get_system_tick();
    arb.masters[master_id].requesting = true;
    arb.masters[master_id].request_time = start;
    
    // Wait for grant
    while (get_system_tick() - start < timeout_ms) {
        if (!arb.bus_busy) {
            int winner = find_highest_priority();
            
            if (winner == master_id) {
                arb.bus_busy = true;
                arb.current_owner = master_id;
                arb.masters[master_id].requesting = false;
                return true;
            }
        }
        delay_us(10);
    }
    
    arb.masters[master_id].requesting = false;
    return false;
}

// Release bus
void arbiter_release_bus(uint8_t master_id) {
    if (arb.current_owner == master_id) {
        arb.bus_busy = false;
        arb.current_owner = 0xFF;
    }
}
```

## Rust Implementation Examples

### Safe Arbitration with Type System

```rust
use core::sync::atomic::{AtomicU8, AtomicBool, Ordering};
use core::cell::UnsafeCell;

const NO_OWNER: u8 = 0xFF;
const MAX_RETRIES: u32 = 1000;

pub struct SPIBusArbiter {
    owner: AtomicU8,
    locked: AtomicBool,
}

impl SPIBusArbiter {
    pub const fn new() -> Self {
        Self {
            owner: AtomicU8::new(NO_OWNER),
            locked: AtomicBool::new(false),
        }
    }
    
    /// Attempt to acquire the bus for a specific master
    pub fn acquire(&self, master_id: u8, timeout_ms: u32) -> Result<BusGuard, ArbiterError> {
        let start = get_system_time_ms();
        let mut retries = 0;
        
        while retries < MAX_RETRIES {
            // Atomic compare-and-swap to claim ownership
            match self.owner.compare_exchange(
                NO_OWNER,
                master_id,
                Ordering::Acquire,
                Ordering::Relaxed
            ) {
                Ok(_) => {
                    self.locked.store(true, Ordering::Release);
                    return Ok(BusGuard { arbiter: self, master_id });
                }
                Err(_) => {
                    // Bus is owned by someone else
                    if get_system_time_ms() - start >= timeout_ms {
                        return Err(ArbiterError::Timeout);
                    }
                    
                    delay_us(50);
                    retries += 1;
                }
            }
        }
        
        Err(ArbiterError::MaxRetriesExceeded)
    }
    
    /// Release the bus (called by BusGuard drop)
    fn release(&self, master_id: u8) {
        let current = self.owner.load(Ordering::Acquire);
        if current == master_id {
            self.locked.store(false, Ordering::Release);
            self.owner.store(NO_OWNER, Ordering::Release);
        }
    }
}

/// RAII guard that automatically releases bus on drop
pub struct BusGuard<'a> {
    arbiter: &'a SPIBusArbiter,
    master_id: u8,
}

impl<'a> Drop for BusGuard<'a> {
    fn drop(&mut self) {
        self.arbiter.release(self.master_id);
    }
}

#[derive(Debug)]
pub enum ArbiterError {
    Timeout,
    MaxRetriesExceeded,
}

// Usage example
static BUS_ARBITER: SPIBusArbiter = SPIBusArbiter::new();

pub fn spi_transaction_with_arbitration(
    master_id: u8,
    data: &[u8]
) -> Result<Vec<u8>, ArbiterError> {
    // Acquire bus - automatically released when guard drops
    let _guard = BUS_ARBITER.acquire(master_id, 100)?;
    
    // Perform SPI transaction while holding the lock
    let mut result = Vec::with_capacity(data.len());
    for &byte in data {
        result.push(spi_transfer_byte(byte));
    }
    
    Ok(result)
    // Bus automatically released here when _guard goes out of scope
}
```

### Hardware Arbiter Interface

```rust
use embedded_hal::digital::v2::{InputPin, OutputPin};

pub struct HardwareArbiter<REQ, GNT> 
where
    REQ: OutputPin,
    GNT: InputPin,
{
    request_pin: REQ,
    grant_pin: GNT,
    timeout_ms: u32,
}

impl<REQ, GNT> HardwareArbiter<REQ, GNT>
where
    REQ: OutputPin,
    GNT: InputPin,
{
    pub fn new(request_pin: REQ, grant_pin: GNT, timeout_ms: u32) -> Self {
        Self {
            request_pin,
            grant_pin,
            timeout_ms,
        }
    }
    
    /// Request bus access and wait for grant
    pub fn request(&mut self) -> Result<HardwareGrant<REQ, GNT>, ArbiterError> {
        let start = get_system_time_ms();
        
        // Assert request line
        self.request_pin.set_high()
            .map_err(|_| ArbiterError::HardwareError)?;
        
        // Wait for grant signal
        loop {
            if self.grant_pin.is_high()
                .map_err(|_| ArbiterError::HardwareError)? {
                return Ok(HardwareGrant { arbiter: self });
            }
            
            if get_system_time_ms() - start >= self.timeout_ms {
                // Timeout - release request
                let _ = self.request_pin.set_low();
                return Err(ArbiterError::Timeout);
            }
            
            delay_us(10);
        }
    }
}

/// RAII guard for hardware-arbitrated bus access
pub struct HardwareGrant<'a, REQ, GNT>
where
    REQ: OutputPin,
    GNT: InputPin,
{
    arbiter: &'a mut HardwareArbiter<REQ, GNT>,
}

impl<'a, REQ, GNT> Drop for HardwareGrant<'a, REQ, GNT>
where
    REQ: OutputPin,
    GNT: InputPin,
{
    fn drop(&mut self) {
        // De-assert request line
        let _ = self.arbiter.request_pin.set_low();
        delay_us(5);  // Brief settling time
    }
}

// Usage with embedded-hal SPI
pub fn perform_arbitrated_spi_transfer<SPI, REQ, GNT>(
    spi: &mut SPI,
    arbiter: &mut HardwareArbiter<REQ, GNT>,
    data: &mut [u8],
) -> Result<(), ArbiterError>
where
    SPI: embedded_hal::blocking::spi::Transfer<u8>,
    REQ: OutputPin,
    GNT: InputPin,
{
    // Request and acquire bus
    let _grant = arbiter.request()?;
    
    // Perform SPI transfer while holding grant
    spi.transfer(data)
        .map_err(|_| ArbiterError::TransferError)?;
    
    Ok(())
    // Grant automatically released here
}
```

### Priority Queue Arbitration

```rust
use core::cmp::Ordering;
use heapless::Vec;
use heapless::binary_heap::{BinaryHeap, Max};

const MAX_MASTERS: usize = 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MasterRequest {
    master_id: u8,
    priority: u8,  // Lower = higher priority
    timestamp: u32,
}

impl Ord for MasterRequest {
    fn cmp(&self, other: &Self) -> Ordering {
        // First compare by priority (lower is better)
        match self.priority.cmp(&other.priority) {
            Ordering::Equal => {
                // If same priority, earlier timestamp wins
                other.timestamp.cmp(&self.timestamp)
            }
            other => other.reverse(),  // Reverse to make lower priority higher
        }
    }
}

impl PartialOrd for MasterRequest {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

pub struct PriorityArbiter {
    requests: BinaryHeap<MasterRequest, Max, MAX_MASTERS>,
    current_owner: Option<u8>,
}

impl PriorityArbiter {
    pub fn new() -> Self {
        Self {
            requests: BinaryHeap::new(),
            current_owner: None,
        }
    }
    
    /// Add a request to the queue
    pub fn request(&mut self, master_id: u8, priority: u8) -> Result<(), ()> {
        let request = MasterRequest {
            master_id,
            priority,
            timestamp: get_system_time_ms(),
        };
        
        self.requests.push(request).map_err(|_| ())
    }
    
    /// Grant bus to highest priority requester
    pub fn grant(&mut self) -> Option<u8> {
        if self.current_owner.is_some() {
            return None;  // Bus still in use
        }
        
        if let Some(request) = self.requests.pop() {
            self.current_owner = Some(request.master_id);
            Some(request.master_id)
        } else {
            None
        }
    }
    
    /// Release the bus
    pub fn release(&mut self, master_id: u8) {
        if self.current_owner == Some(master_id) {
            self.current_owner = None;
        }
    }
}
```

## Best Practices for Shared Bus Arbitration

**Design Recommendations:**

1. **Avoid if possible** - Use separate SPI buses or a single master with message passing
2. **Hardware arbitration preferred** - More reliable than software approaches
3. **Use tri-state buffers** - Prevent electrical conflicts when masters are inactive
4. **Implement timeouts** - Prevent deadlocks from failed masters
5. **Add fairness mechanisms** - Prevent priority inversion or starvation
6. **Consider performance impact** - Arbitration adds overhead to every transaction

**Common Pitfalls:**

- Electrical conflicts from simultaneous driving
- Race conditions in software arbitration
- Deadlocks from improper release logic
- Starvation of low-priority masters
- Insufficient timeout handling

## Summary

Shared Bus Arbitration in SPI is a complex challenge that requires careful design since the protocol lacks native multi-master support. While hardware-based arbitration using external circuits provides the most robust solution, software approaches using atomic operations and proper synchronization primitives can work in controlled environments. The key is ensuring that only one master drives the bus at any time, implementing proper timeout and error handling, and using RAII patterns (in C++ and Rust) to guarantee bus release even when errors occur. Given the complexity and performance overhead, it's generally better to use separate SPI buses or implement a single-master architecture with message-passing between processors when possible. However, when multiple masters are unavoidable, the implementations shown provide reliable patterns for safe bus sharing with proper priority handling and deadlock prevention.