# CAN Bus Guardians

## Detailed Description

**Bus Guardians** are critical safety mechanisms in CAN (Controller Area Network) systems designed to protect the network from **babbling nodes** - faulty nodes that continuously transmit messages without proper timing or arbitration, potentially monopolizing the bus and preventing legitimate communication.

### The Problem: Babbling Nodes

A babbling node can occur due to:
- **Hardware failures**: Defective CAN controllers or transceivers
- **Software bugs**: Infinite loops sending messages, missing error handling
- **EMI/radiation**: Causing unintended transmissions
- **Bit errors**: Corrupting transmission logic

Without protection, a single babbling node can bring down an entire CAN network, making it critical for safety-critical systems (automotive, industrial, medical).

### Bus Guardian Mechanisms

Bus guardians operate at both hardware and software levels:

#### Hardware Bus Guardians
- **Physical isolation**: Relays or switches that can disconnect faulty nodes
- **Watchdog timers**: Monitor transmission patterns and cut power/connection
- **Dedicated guardian ICs**: Specialized chips like Infineon TLE6251G
- **Time-triggered buses**: FlexRay-style TDMA with guardian enforcement

#### Software Bus Guardians
- **Transmission rate limiting**: Monitor and throttle excessive transmissions
- **Error frame detection**: Track bus-off conditions and anomalies
- **Message scheduling**: Enforce timing constraints on transmissions
- **Health monitoring**: Detect abnormal patterns and trigger isolation

---

## Code Examples

### C/C++ Implementation

```c
// Software Bus Guardian for CAN
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_TX_PER_WINDOW 10    // Max transmissions per time window
#define TIME_WINDOW_MS 100      // Monitoring window
#define MAX_BUS_OFF_COUNT 3     // Max bus-off events before isolation
#define BABBLING_THRESHOLD 50   // Messages/sec threshold

typedef struct {
    uint32_t tx_count;          // Transmissions in current window
    uint32_t total_tx;          // Total transmissions
    uint32_t bus_off_count;     // Bus-off error count
    uint32_t error_count;       // Total error count
    time_t window_start;        // Window start time
    bool is_isolated;           // Node isolation status
    bool babbling_detected;     // Babbling flag
} BusGuardian;

// Initialize bus guardian
void bus_guardian_init(BusGuardian *guardian) {
    guardian->tx_count = 0;
    guardian->total_tx = 0;
    guardian->bus_off_count = 0;
    guardian->error_count = 0;
    guardian->window_start = time(NULL);
    guardian->is_isolated = false;
    guardian->babbling_detected = false;
}

// Check if transmission is allowed
bool bus_guardian_check_tx(BusGuardian *guardian) {
    if (guardian->is_isolated) {
        return false;  // Node is isolated
    }
    
    time_t current_time = time(NULL);
    double elapsed = difftime(current_time, guardian->window_start);
    
    // Reset window if time expired
    if (elapsed * 1000 >= TIME_WINDOW_MS) {
        guardian->tx_count = 0;
        guardian->window_start = current_time;
    }
    
    // Check transmission rate
    if (guardian->tx_count >= MAX_TX_PER_WINDOW) {
        guardian->babbling_detected = true;
        return false;  // Rate limit exceeded
    }
    
    return true;
}

// Record successful transmission
void bus_guardian_record_tx(BusGuardian *guardian) {
    guardian->tx_count++;
    guardian->total_tx++;
}

// Record bus-off event
void bus_guardian_record_bus_off(BusGuardian *guardian) {
    guardian->bus_off_count++;
    
    // Isolate if threshold exceeded
    if (guardian->bus_off_count >= MAX_BUS_OFF_COUNT) {
        guardian->is_isolated = true;
        guardian->babbling_detected = true;
    }
}

// Record CAN error
void bus_guardian_record_error(BusGuardian *guardian) {
    guardian->error_count++;
}

// Get guardian status
void bus_guardian_get_status(BusGuardian *guardian, 
                             char *status_str, 
                             size_t max_len) {
    snprintf(status_str, max_len,
             "TX: %u, Errors: %u, Bus-Off: %u, Isolated: %s, Babbling: %s",
             guardian->total_tx,
             guardian->error_count,
             guardian->bus_off_count,
             guardian->is_isolated ? "YES" : "NO",
             guardian->babbling_detected ? "YES" : "NO");
}

// Reset guardian (requires authorization)
void bus_guardian_reset(BusGuardian *guardian) {
    guardian->bus_off_count = 0;
    guardian->error_count = 0;
    guardian->is_isolated = false;
    guardian->babbling_detected = false;
}

// Example usage with SocketCAN
#ifdef __linux__
#include <linux/can.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>

int can_send_with_guardian(int sock, 
                           struct can_frame *frame,
                           BusGuardian *guardian) {
    // Check if transmission allowed
    if (!bus_guardian_check_tx(guardian)) {
        if (guardian->babbling_detected) {
            fprintf(stderr, "ERROR: Babbling detected - transmission blocked\n");
        }
        return -1;
    }
    
    // Send CAN frame
    ssize_t bytes = write(sock, frame, sizeof(struct can_frame));
    
    if (bytes == sizeof(struct can_frame)) {
        bus_guardian_record_tx(guardian);
        return 0;
    } else {
        bus_guardian_record_error(guardian);
        return -1;
    }
}
#endif
```

### Rust Implementation

```rust
use std::time::{Duration, Instant};

const MAX_TX_PER_WINDOW: u32 = 10;
const TIME_WINDOW: Duration = Duration::from_millis(100);
const MAX_BUS_OFF_COUNT: u32 = 3;
const BABBLING_THRESHOLD: u32 = 50;

#[derive(Debug, Clone)]
pub struct BusGuardian {
    tx_count: u32,
    total_tx: u64,
    bus_off_count: u32,
    error_count: u32,
    window_start: Instant,
    is_isolated: bool,
    babbling_detected: bool,
}

#[derive(Debug, Clone, Copy)]
pub enum GuardianError {
    RateLimitExceeded,
    NodeIsolated,
    BabblingDetected,
    BusOffLimitExceeded,
}

impl BusGuardian {
    /// Create a new bus guardian
    pub fn new() -> Self {
        Self {
            tx_count: 0,
            total_tx: 0,
            bus_off_count: 0,
            error_count: 0,
            window_start: Instant::now(),
            is_isolated: false,
            babbling_detected: false,
        }
    }

    /// Check if transmission is allowed
    pub fn check_transmission(&mut self) -> Result<(), GuardianError> {
        if self.is_isolated {
            return Err(GuardianError::NodeIsolated);
        }

        if self.babbling_detected {
            return Err(GuardianError::BabblingDetected);
        }

        // Check time window
        let elapsed = self.window_start.elapsed();
        if elapsed >= TIME_WINDOW {
            self.tx_count = 0;
            self.window_start = Instant::now();
        }

        // Check rate limit
        if self.tx_count >= MAX_TX_PER_WINDOW {
            self.babbling_detected = true;
            return Err(GuardianError::RateLimitExceeded);
        }

        Ok(())
    }

    /// Record successful transmission
    pub fn record_transmission(&mut self) {
        self.tx_count += 1;
        self.total_tx += 1;
    }

    /// Record bus-off event
    pub fn record_bus_off(&mut self) {
        self.bus_off_count += 1;

        if self.bus_off_count >= MAX_BUS_OFF_COUNT {
            self.is_isolated = true;
            self.babbling_detected = true;
        }
    }

    /// Record CAN error
    pub fn record_error(&mut self) {
        self.error_count += 1;
    }

    /// Get guardian statistics
    pub fn get_stats(&self) -> GuardianStats {
        GuardianStats {
            total_transmissions: self.total_tx,
            error_count: self.error_count,
            bus_off_count: self.bus_off_count,
            is_isolated: self.is_isolated,
            babbling_detected: self.babbling_detected,
        }
    }

    /// Reset guardian (requires authorization)
    pub fn reset(&mut self) {
        self.bus_off_count = 0;
        self.error_count = 0;
        self.is_isolated = false;
        self.babbling_detected = false;
    }

    /// Get current transmission rate
    pub fn get_tx_rate(&self) -> f64 {
        let elapsed = self.window_start.elapsed().as_secs_f64();
        if elapsed > 0.0 {
            self.tx_count as f64 / elapsed
        } else {
            0.0
        }
    }
}

#[derive(Debug, Clone)]
pub struct GuardianStats {
    pub total_transmissions: u64,
    pub error_count: u32,
    pub bus_off_count: u32,
    pub is_isolated: bool,
    pub babbling_detected: bool,
}

// Example integration with socketcan-rs
#[cfg(feature = "socketcan")]
pub mod socketcan_integration {
    use super::*;
    use socketcan::{CanSocket, CanFrame};
    use std::io;

    pub struct GuardedCanSocket {
        socket: CanSocket,
        guardian: BusGuardian,
    }

    impl GuardedCanSocket {
        pub fn new(socket: CanSocket) -> Self {
            Self {
                socket,
                guardian: BusGuardian::new(),
            }
        }

        /// Send frame with guardian protection
        pub fn send_frame(&mut self, frame: &CanFrame) -> Result<(), io::Error> {
            // Check guardian
            if let Err(e) = self.guardian.check_transmission() {
                eprintln!("Guardian blocked transmission: {:?}", e);
                return Err(io::Error::new(
                    io::ErrorKind::PermissionDenied,
                    format!("{:?}", e),
                ));
            }

            // Send frame
            match self.socket.write_frame(frame) {
                Ok(_) => {
                    self.guardian.record_transmission();
                    Ok(())
                }
                Err(e) => {
                    self.guardian.record_error();
                    Err(e)
                }
            }
        }

        /// Get guardian statistics
        pub fn get_guardian_stats(&self) -> GuardianStats {
            self.guardian.get_stats()
        }

        /// Reset guardian
        pub fn reset_guardian(&mut self) {
            self.guardian.reset();
        }
    }
}

// Example advanced guardian with configurable policies
pub struct AdvancedGuardian {
    basic_guardian: BusGuardian,
    config: GuardianConfig,
}

#[derive(Debug, Clone)]
pub struct GuardianConfig {
    pub max_tx_per_window: u32,
    pub time_window: Duration,
    pub max_bus_off_count: u32,
    pub enable_auto_reset: bool,
    pub auto_reset_delay: Duration,
}

impl Default for GuardianConfig {
    fn default() -> Self {
        Self {
            max_tx_per_window: MAX_TX_PER_WINDOW,
            time_window: TIME_WINDOW,
            max_bus_off_count: MAX_BUS_OFF_COUNT,
            enable_auto_reset: false,
            auto_reset_delay: Duration::from_secs(60),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_rate_limiting() {
        let mut guardian = BusGuardian::new();

        // Should allow up to MAX_TX_PER_WINDOW transmissions
        for _ in 0..MAX_TX_PER_WINDOW {
            assert!(guardian.check_transmission().is_ok());
            guardian.record_transmission();
        }

        // Next transmission should fail
        assert!(matches!(
            guardian.check_transmission(),
            Err(GuardianError::RateLimitExceeded)
        ));
    }

    #[test]
    fn test_bus_off_isolation() {
        let mut guardian = BusGuardian::new();

        // Record bus-off events
        for _ in 0..MAX_BUS_OFF_COUNT {
            guardian.record_bus_off();
        }

        // Node should be isolated
        assert!(matches!(
            guardian.check_transmission(),
            Err(GuardianError::NodeIsolated)
        ));
    }

    #[test]
    fn test_reset() {
        let mut guardian = BusGuardian::new();
        
        guardian.record_bus_off();
        guardian.reset();
        
        let stats = guardian.get_stats();
        assert_eq!(stats.bus_off_count, 0);
        assert!(!stats.is_isolated);
    }
}
```

---

## Summary

**Bus Guardians** are essential protective mechanisms that prevent babbling nodes from disrupting CAN networks. They combine:

- **Hardware protection**: Physical isolation, watchdogs, and dedicated guardian ICs
- **Software monitoring**: Rate limiting, error tracking, and pattern detection
- **Isolation strategies**: Automatic disconnection of faulty nodes

Key implementation features include:
- **Transmission rate limiting** to detect excessive messaging
- **Bus-off event tracking** to identify controller failures
- **Automatic isolation** when thresholds are exceeded
- **Statistics gathering** for diagnostics and debugging
- **Reset mechanisms** for recovery after fault resolution

Both C/C++ and Rust implementations provide robust protection through configurable policies, real-time monitoring, and fail-safe isolation. These guardians are critical for safety-critical systems where a single faulty node could compromise the entire network's operation.