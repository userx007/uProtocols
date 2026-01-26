# CAN Bus-Off Recovery Strategies

## Overview

The **Bus-Off state** is a critical error management mechanism in CAN (Controller Area Network) that protects the network from faulty nodes. When a node experiences persistent transmission errors, it transitions through error states (Error-Active → Error-Passive → Bus-Off) and must implement proper recovery strategies to rejoin the network safely.

## Understanding Bus-Off State

### Error Counter Mechanism

CAN controllers maintain two error counters:
- **Transmit Error Counter (TEC)**: Incremented on transmission errors
- **Receive Error Counter (REC)**: Incremented on reception errors

**State Transitions:**
- **Error-Active**: TEC < 128 and REC < 128 (normal operation)
- **Error-Passive**: TEC ≥ 128 or REC ≥ 128 (limited error signaling)
- **Bus-Off**: TEC ≥ 256 (node disconnected from bus)

### Why Bus-Off Occurs

Common causes include:
- Hardware failures (transceiver damage, wiring issues)
- Bit timing mismatches
- Persistent network collisions
- Software bugs causing invalid frames
- EMI/noise overwhelming error correction

## Recovery Strategies

### 1. Automatic Recovery

The node automatically attempts to rejoin after monitoring 128×11 consecutive recessive bits (bus idle condition).

**Pros:**
- Simple implementation
- Fast recovery from transient errors
- No external intervention needed

**Cons:**
- May repeatedly fail if underlying issue persists
- Can cause bus disruption if fault is permanent

### 2. Manual Recovery with Backoff

Implements exponential backoff delays between recovery attempts.

**Algorithm:**
```
Initial delay: T₀
Attempt n: delay = min(T₀ × 2ⁿ, T_max)
Reset counter on successful recovery
```

**Pros:**
- Reduces bus load from failing node
- Allows time for transient issues to clear
- Configurable to application needs

**Cons:**
- Slower recovery
- Requires state management

### 3. Supervised Recovery

Application monitors bus-off events and decides recovery policy based on:
- Error frequency
- System state
- Criticality of the node
- Network health metrics

### 4. Conditional Recovery

Recovery only after:
- Diagnostic verification
- Hardware self-test
- Network quality assessment
- External authorization signal

## C/C++ Implementation Examples

### Example 1: Basic Automatic Recovery (Linux SocketCAN)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <time.h>

typedef struct {
    int sockfd;
    char ifname[IFNAMSIZ];
    int bus_off_count;
    time_t last_bus_off;
    int recovery_mode; // 0=auto, 1=manual
} can_bus_t;

// Initialize CAN interface
int can_init(can_bus_t *bus, const char *ifname, int recovery_mode) {
    struct sockaddr_can addr;
    struct ifreq ifr;
    
    bus->sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (bus->sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    strncpy(bus->ifname, ifname, IFNAMSIZ - 1);
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(bus->sockfd, SIOCGIFINDEX, &ifr);
    
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    // Enable error frame reception
    can_err_mask_t err_mask = CAN_ERR_BUSOFF | CAN_ERR_CRTL;
    setsockopt(bus->sockfd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));
    
    if (bind(bus->sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(bus->sockfd);
        return -1;
    }
    
    bus->bus_off_count = 0;
    bus->last_bus_off = 0;
    bus->recovery_mode = recovery_mode;
    
    printf("CAN interface %s initialized\n", ifname);
    return 0;
}

// Automatic recovery - restart interface
int can_auto_recover(can_bus_t *bus) {
    char cmd[256];
    
    printf("Attempting automatic recovery...\n");
    
    // Bring interface down
    snprintf(cmd, sizeof(cmd), "ip link set %s down", bus->ifname);
    system(cmd);
    usleep(100000); // 100ms delay
    
    // Bring interface up
    snprintf(cmd, sizeof(cmd), "ip link set %s up", bus->ifname);
    if (system(cmd) == 0) {
        printf("Interface %s recovered\n", bus->ifname);
        return 0;
    }
    
    return -1;
}

// Manual recovery with exponential backoff
int can_manual_recover(can_bus_t *bus) {
    const int base_delay = 1; // 1 second
    const int max_delay = 60; // 60 seconds max
    
    time_t now = time(NULL);
    time_t elapsed = now - bus->last_bus_off;
    
    // Calculate backoff delay: min(base * 2^attempts, max)
    int delay = base_delay;
    for (int i = 0; i < bus->bus_off_count && delay < max_delay; i++) {
        delay *= 2;
    }
    if (delay > max_delay) delay = max_delay;
    
    printf("Bus-off count: %d, waiting %d seconds before recovery...\n",
           bus->bus_off_count, delay);
    
    if (elapsed < delay) {
        printf("Still in backoff period (%ld/%d seconds)\n", elapsed, delay);
        return -1;
    }
    
    return can_auto_recover(bus);
}

// Handle bus-off error
void handle_bus_off(can_bus_t *bus) {
    bus->bus_off_count++;
    bus->last_bus_off = time(NULL);
    
    printf("BUS-OFF detected! (occurrence #%d)\n", bus->bus_off_count);
    
    if (bus->recovery_mode == 0) {
        // Automatic recovery
        can_auto_recover(bus);
    } else {
        // Manual recovery with backoff
        can_manual_recover(bus);
    }
}

// Main receive loop with error handling
void can_receive_loop(can_bus_t *bus) {
    struct can_frame frame;
    int nbytes;
    
    printf("Starting CAN receive loop...\n");
    
    while (1) {
        nbytes = read(bus->sockfd, &frame, sizeof(struct can_frame));
        
        if (nbytes < 0) {
            perror("CAN read error");
            continue;
        }
        
        if (nbytes < sizeof(struct can_frame)) {
            fprintf(stderr, "Incomplete CAN frame\n");
            continue;
        }
        
        // Check for error frame
        if (frame.can_id & CAN_ERR_FLAG) {
            if (frame.can_id & CAN_ERR_BUSOFF) {
                handle_bus_off(bus);
            }
            else if (frame.can_id & CAN_ERR_CRTL) {
                printf("Controller error detected\n");
            }
        } else {
            // Normal data frame
            printf("RX: ID=0x%03X DLC=%d Data=", 
                   frame.can_id, frame.can_dlc);
            for (int i = 0; i < frame.can_dlc; i++) {
                printf("%02X ", frame.data[i]);
            }
            printf("\n");
        }
    }
}

// Cleanup
void can_close(can_bus_t *bus) {
    close(bus->sockfd);
    printf("CAN interface closed\n");
}

int main(int argc, char *argv[]) {
    can_bus_t bus;
    
    if (argc < 2) {
        printf("Usage: %s <can_interface> [recovery_mode]\n", argv[0]);
        printf("  recovery_mode: 0=automatic (default), 1=manual with backoff\n");
        return 1;
    }
    
    int recovery_mode = (argc > 2) ? atoi(argv[2]) : 0;
    
    if (can_init(&bus, argv[1], recovery_mode) < 0) {
        return 1;
    }
    
    can_receive_loop(&bus);
    can_close(&bus);
    
    return 0;
}
```

### Example 2: Advanced Recovery with State Machine (Embedded C)

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Recovery state machine
typedef enum {
    RECOVERY_STATE_IDLE,
    RECOVERY_STATE_WAITING,
    RECOVERY_STATE_ATTEMPTING,
    RECOVERY_STATE_VERIFYING,
    RECOVERY_STATE_FAILED
} recovery_state_t;

// Recovery configuration
typedef struct {
    uint32_t base_backoff_ms;      // Initial backoff time
    uint32_t max_backoff_ms;       // Maximum backoff time
    uint8_t max_attempts;          // Max consecutive attempts
    uint32_t verification_time_ms; // Time to verify recovery
    bool require_diagnostics;      // Run diagnostics before recovery
} recovery_config_t;

// Recovery statistics
typedef struct {
    uint32_t total_bus_offs;
    uint32_t successful_recoveries;
    uint32_t failed_recoveries;
    uint32_t last_recovery_time_ms;
    uint32_t max_recovery_time_ms;
} recovery_stats_t;

// Recovery manager
typedef struct {
    recovery_state_t state;
    recovery_config_t config;
    recovery_stats_t stats;
    
    uint8_t attempt_count;
    uint32_t backoff_start_time;
    uint32_t current_backoff_ms;
    uint32_t recovery_start_time;
    
    bool (*hw_reset_fn)(void);
    bool (*hw_test_fn)(void);
    uint32_t (*get_tick_fn)(void);
} recovery_manager_t;

// Initialize recovery manager
void recovery_init(recovery_manager_t *mgr, 
                   const recovery_config_t *config,
                   bool (*hw_reset)(void),
                   bool (*hw_test)(void),
                   uint32_t (*get_tick)(void)) {
    memset(mgr, 0, sizeof(recovery_manager_t));
    mgr->config = *config;
    mgr->state = RECOVERY_STATE_IDLE;
    mgr->hw_reset_fn = hw_reset;
    mgr->hw_test_fn = hw_test;
    mgr->get_tick_fn = get_tick;
}

// Calculate exponential backoff
static uint32_t calculate_backoff(recovery_manager_t *mgr) {
    uint32_t backoff = mgr->config.base_backoff_ms;
    
    // Exponential: base * 2^attempts
    for (uint8_t i = 0; i < mgr->attempt_count; i++) {
        backoff *= 2;
        if (backoff >= mgr->config.max_backoff_ms) {
            return mgr->config.max_backoff_ms;
        }
    }
    
    return backoff;
}

// Start recovery process
void recovery_start_bus_off(recovery_manager_t *mgr) {
    mgr->stats.total_bus_offs++;
    mgr->state = RECOVERY_STATE_WAITING;
    mgr->attempt_count = 0;
    mgr->current_backoff_ms = calculate_backoff(mgr);
    mgr->backoff_start_time = mgr->get_tick_fn();
    
    printf("Bus-off #%lu detected, entering recovery mode\n", 
           mgr->stats.total_bus_offs);
}

// Process recovery state machine
bool recovery_process(recovery_manager_t *mgr, bool bus_error_active) {
    uint32_t now = mgr->get_tick_fn();
    uint32_t elapsed;
    
    switch (mgr->state) {
        case RECOVERY_STATE_IDLE:
            // Normal operation
            return true;
            
        case RECOVERY_STATE_WAITING:
            // Wait for backoff period
            elapsed = now - mgr->backoff_start_time;
            if (elapsed >= mgr->current_backoff_ms) {
                printf("Backoff period complete, attempting recovery...\n");
                mgr->state = RECOVERY_STATE_ATTEMPTING;
                mgr->recovery_start_time = now;
            }
            return false;
            
        case RECOVERY_STATE_ATTEMPTING:
            // Run diagnostics if required
            if (mgr->config.require_diagnostics) {
                if (mgr->hw_test_fn && !mgr->hw_test_fn()) {
                    printf("Hardware diagnostics failed\n");
                    mgr->stats.failed_recoveries++;
                    mgr->state = RECOVERY_STATE_FAILED;
                    return false;
                }
            }
            
            // Attempt hardware reset
            if (mgr->hw_reset_fn && mgr->hw_reset_fn()) {
                printf("Hardware reset successful, verifying...\n");
                mgr->state = RECOVERY_STATE_VERIFYING;
                mgr->backoff_start_time = now;
            } else {
                printf("Hardware reset failed\n");
                mgr->attempt_count++;
                
                if (mgr->attempt_count >= mgr->config.max_attempts) {
                    printf("Max recovery attempts exceeded\n");
                    mgr->stats.failed_recoveries++;
                    mgr->state = RECOVERY_STATE_FAILED;
                } else {
                    // Calculate new backoff and retry
                    mgr->current_backoff_ms = calculate_backoff(mgr);
                    mgr->state = RECOVERY_STATE_WAITING;
                    mgr->backoff_start_time = now;
                }
            }
            return false;
            
        case RECOVERY_STATE_VERIFYING:
            // Wait for verification period
            elapsed = now - mgr->backoff_start_time;
            
            if (bus_error_active) {
                printf("Bus error still active during verification\n");
                mgr->attempt_count++;
                
                if (mgr->attempt_count >= mgr->config.max_attempts) {
                    mgr->stats.failed_recoveries++;
                    mgr->state = RECOVERY_STATE_FAILED;
                } else {
                    mgr->current_backoff_ms = calculate_backoff(mgr);
                    mgr->state = RECOVERY_STATE_WAITING;
                    mgr->backoff_start_time = now;
                }
                return false;
            }
            
            if (elapsed >= mgr->config.verification_time_ms) {
                uint32_t recovery_time = now - mgr->recovery_start_time;
                printf("Recovery successful! (took %lu ms)\n", recovery_time);
                
                mgr->stats.successful_recoveries++;
                mgr->stats.last_recovery_time_ms = recovery_time;
                if (recovery_time > mgr->stats.max_recovery_time_ms) {
                    mgr->stats.max_recovery_time_ms = recovery_time;
                }
                
                mgr->state = RECOVERY_STATE_IDLE;
                mgr->attempt_count = 0;
                return true;
            }
            return false;
            
        case RECOVERY_STATE_FAILED:
            // Permanent failure - require manual intervention
            printf("Recovery failed, manual intervention required\n");
            return false;
            
        default:
            return false;
    }
}

// Get recovery statistics
void recovery_get_stats(recovery_manager_t *mgr, recovery_stats_t *stats) {
    *stats = mgr->stats;
}

// Reset recovery manager (for manual intervention)
void recovery_reset(recovery_manager_t *mgr) {
    mgr->state = RECOVERY_STATE_IDLE;
    mgr->attempt_count = 0;
    printf("Recovery manager manually reset\n");
}
```

## Rust Implementation Examples

### Example 1: Safe Recovery Manager with Type State Pattern

```rust
use std::time::{Duration, Instant};
use std::marker::PhantomData;

// Type-state pattern for compile-time state verification
mod state {
    pub struct Idle;
    pub struct Waiting;
    pub struct Recovering;
    pub struct Failed;
}

#[derive(Debug, Clone)]
pub struct RecoveryConfig {
    pub base_backoff: Duration,
    pub max_backoff: Duration,
    pub max_attempts: u8,
    pub verification_time: Duration,
    pub require_diagnostics: bool,
}

impl Default for RecoveryConfig {
    fn default() -> Self {
        Self {
            base_backoff: Duration::from_secs(1),
            max_backoff: Duration::from_secs(60),
            max_attempts: 5,
            verification_time: Duration::from_millis(500),
            require_diagnostics: false,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct RecoveryStats {
    pub total_bus_offs: u64,
    pub successful_recoveries: u64,
    pub failed_recoveries: u64,
    pub last_recovery_duration: Option<Duration>,
    pub max_recovery_duration: Duration,
}

pub struct RecoveryManager<State = state::Idle> {
    config: RecoveryConfig,
    stats: RecoveryStats,
    attempt_count: u8,
    backoff_start: Option<Instant>,
    recovery_start: Option<Instant>,
    current_backoff: Duration,
    _state: PhantomData<State>,
}

impl RecoveryManager<state::Idle> {
    pub fn new(config: RecoveryConfig) -> Self {
        Self {
            config,
            stats: RecoveryStats::default(),
            attempt_count: 0,
            backoff_start: None,
            recovery_start: None,
            current_backoff: Duration::from_secs(0),
            _state: PhantomData,
        }
    }
    
    /// Transition to waiting state on bus-off detection
    pub fn handle_bus_off(mut self) -> RecoveryManager<state::Waiting> {
        self.stats.total_bus_offs += 1;
        self.attempt_count = 0;
        self.current_backoff = self.calculate_backoff();
        self.backoff_start = Some(Instant::now());
        
        println!("Bus-off #{} detected, entering backoff for {:?}",
                 self.stats.total_bus_offs, self.current_backoff);
        
        RecoveryManager {
            config: self.config,
            stats: self.stats,
            attempt_count: self.attempt_count,
            backoff_start: self.backoff_start,
            recovery_start: None,
            current_backoff: self.current_backoff,
            _state: PhantomData,
        }
    }
    
    pub fn stats(&self) -> &RecoveryStats {
        &self.stats
    }
}

impl RecoveryManager<state::Waiting> {
    /// Check if backoff period is complete
    pub fn check_backoff(self) -> Result<RecoveryManager<state::Recovering>, Self> {
        if let Some(start) = self.backoff_start {
            if start.elapsed() >= self.current_backoff {
                println!("Backoff complete, attempting recovery...");
                return Ok(RecoveryManager {
                    config: self.config,
                    stats: self.stats,
                    attempt_count: self.attempt_count,
                    backoff_start: None,
                    recovery_start: Some(Instant::now()),
                    current_backoff: self.current_backoff,
                    _state: PhantomData,
                });
            }
        }
        Err(self)
    }
}

impl RecoveryManager<state::Recovering> {
    /// Attempt recovery with hardware reset
    pub fn attempt_recovery<F, D>(
        mut self,
        hw_reset: F,
        hw_diagnostics: Option<D>,
    ) -> RecoveryResult
    where
        F: FnOnce() -> Result<(), String>,
        D: FnOnce() -> Result<(), String>,
    {
        // Run diagnostics if required
        if self.config.require_diagnostics {
            if let Some(diag) = hw_diagnostics {
                if let Err(e) = diag() {
                    println!("Diagnostics failed: {}", e);
                    return self.handle_recovery_failure();
                }
            }
        }
        
        // Attempt hardware reset
        match hw_reset() {
            Ok(()) => {
                println!("Hardware reset successful, verification required");
                RecoveryResult::NeedsVerification(RecoveryManager {
                    config: self.config,
                    stats: self.stats,
                    attempt_count: self.attempt_count,
                    backoff_start: Some(Instant::now()),
                    recovery_start: self.recovery_start,
                    current_backoff: self.current_backoff,
                    _state: PhantomData,
                })
            }
            Err(e) => {
                println!("Hardware reset failed: {}", e);
                self.handle_recovery_failure()
            }
        }
    }
    
    /// Verify recovery success
    pub fn verify_recovery(
        mut self,
        bus_healthy: bool,
    ) -> RecoveryResult {
        if !bus_healthy {
            println!("Bus still unhealthy during verification");
            return self.handle_recovery_failure();
        }
        
        let verification_start = self.backoff_start.unwrap();
        if verification_start.elapsed() < self.config.verification_time {
            // Still verifying
            return RecoveryResult::NeedsVerification(self);
        }
        
        // Recovery successful
        if let Some(start) = self.recovery_start {
            let duration = start.elapsed();
            println!("Recovery successful! Took {:?}", duration);
            
            self.stats.successful_recoveries += 1;
            self.stats.last_recovery_duration = Some(duration);
            if duration > self.stats.max_recovery_duration {
                self.stats.max_recovery_duration = duration;
            }
        }
        
        RecoveryResult::Success(RecoveryManager {
            config: self.config,
            stats: self.stats,
            attempt_count: 0,
            backoff_start: None,
            recovery_start: None,
            current_backoff: Duration::from_secs(0),
            _state: PhantomData,
        })
    }
    
    fn handle_recovery_failure(mut self) -> RecoveryResult {
        self.attempt_count += 1;
        
        if self.attempt_count >= self.config.max_attempts {
            println!("Max recovery attempts ({}) exceeded", self.config.max_attempts);
            self.stats.failed_recoveries += 1;
            
            RecoveryResult::Failed(RecoveryManager {
                config: self.config,
                stats: self.stats,
                attempt_count: self.attempt_count,
                backoff_start: None,
                recovery_start: None,
                current_backoff: Duration::from_secs(0),
                _state: PhantomData,
            })
        } else {
            // Retry with longer backoff
            self.current_backoff = self.calculate_backoff();
            println!("Retrying after {:?} (attempt {}/{})",
                     self.current_backoff, self.attempt_count, 
                     self.config.max_attempts);
            
            RecoveryResult::Retry(RecoveryManager {
                config: self.config,
                stats: self.stats,
                attempt_count: self.attempt_count,
                backoff_start: Some(Instant::now()),
                recovery_start: None,
                current_backoff: self.current_backoff,
                _state: PhantomData,
            })
        }
    }
}

impl RecoveryManager<state::Failed> {
    /// Manual reset after failure
    pub fn manual_reset(mut self) -> RecoveryManager<state::Idle> {
        println!("Manual reset initiated");
        RecoveryManager {
            config: self.config,
            stats: self.stats,
            attempt_count: 0,
            backoff_start: None,
            recovery_start: None,
            current_backoff: Duration::from_secs(0),
            _state: PhantomData,
        }
    }
    
    pub fn stats(&self) -> &RecoveryStats {
        &self.stats
    }
}

// Helper trait for backoff calculation
trait BackoffCalculator {
    fn calculate_backoff(&self) -> Duration;
}

impl<S> BackoffCalculator for RecoveryManager<S> {
    fn calculate_backoff(&self) -> Duration {
        let mut backoff = self.config.base_backoff;
        
        for _ in 0..self.attempt_count {
            backoff = backoff.saturating_mul(2);
            if backoff >= self.config.max_backoff {
                return self.config.max_backoff;
            }
        }
        
        backoff
    }
}

pub enum RecoveryResult {
    Success(RecoveryManager<state::Idle>),
    Retry(RecoveryManager<state::Waiting>),
    NeedsVerification(RecoveryManager<state::Recovering>),
    Failed(RecoveryManager<state::Failed>),
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_recovery_flow() {
        let config = RecoveryConfig::default();
        let mut manager = RecoveryManager::new(config);
        
        // Simulate bus-off
        let mut manager = manager.handle_bus_off();
        
        // Wait for backoff
        std::thread::sleep(Duration::from_secs(2));
        let manager = manager.check_backoff().unwrap();
        
        // Attempt recovery
        let result = manager.attempt_recovery(
            || Ok(()),  // Successful reset
            None,       // No diagnostics
        );
        
        match result {
            RecoveryResult::NeedsVerification(mut mgr) => {
                std::thread::sleep(Duration::from_millis(600));
                let result = mgr.verify_recovery(true);
                assert!(matches!(result, RecoveryResult::Success(_)));
            }
            _ => panic!("Expected verification state"),
        }
    }
}
```

### Example 2: Async Recovery with Tokio

```rust
use tokio::time::{sleep, Duration, Instant};
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Debug, Clone)]
pub struct AsyncRecoveryConfig {
    pub base_backoff: Duration,
    pub max_backoff: Duration,
    pub max_attempts: usize,
    pub health_check_interval: Duration,
}

#[derive(Debug, Clone)]
pub struct BusHealthMetrics {
    pub error_rate: f64,
    pub last_error: Option<Instant>,
    pub consecutive_successes: u32,
}

pub struct AsyncRecoveryManager {
    config: AsyncRecoveryConfig,
    attempt_count: Arc<RwLock<usize>>,
    is_recovering: Arc<RwLock<bool>>,
    health_metrics: Arc<RwLock<BusHealthMetrics>>,
}

impl AsyncRecoveryManager {
    pub fn new(config: AsyncRecoveryConfig) -> Self {
        Self {
            config,
            attempt_count: Arc::new(RwLock::new(0)),
            is_recovering: Arc::new(RwLock::new(false)),
            health_metrics: Arc::new(RwLock::new(BusHealthMetrics {
                error_rate: 0.0,
                last_error: None,
                consecutive_successes: 0,
            })),
        }
    }
    
    /// Start recovery process with exponential backoff
    pub async fn recover_from_bus_off<F, Fut>(
        &self,
        hw_reset: F,
    ) -> Result<(), String>
    where
        F: Fn() -> Fut,
        Fut: std::future::Future<Output = Result<(), String>>,
    {
        let mut is_recovering = self.is_recovering.write().await;
        if *is_recovering {
            return Err("Recovery already in progress".to_string());
        }
        *is_recovering = true;
        drop(is_recovering);
        
        let result = self.recovery_loop(hw_reset).await;
        
        *self.is_recovering.write().await = false;
        result
    }
    
    async fn recovery_loop<F, Fut>(&self, hw_reset: F) -> Result<(), String>
    where
        F: Fn() -> Fut,
        Fut: std::future::Future<Output = Result<(), String>>,
    {
        *self.attempt_count.write().await = 0;
        
        loop {
            let attempt = *self.attempt_count.read().await;
            
            if attempt >= self.config.max_attempts {
                return Err(format!(
                    "Max recovery attempts ({}) exceeded",
                    self.config.max_attempts
                ));
            }
            
            // Calculate backoff
            let backoff = self.calculate_backoff(attempt).await;
            println!("Attempt {}/{}: Waiting {:?} before recovery",
                     attempt + 1, self.config.max_attempts, backoff);
            
            sleep(backoff).await;
            
            // Attempt reset
            println!("Attempting hardware reset...");
            match hw_reset().await {
                Ok(()) => {
                    println!("Reset successful, verifying bus health...");
                    
                    // Verify recovery
                    if self.verify_bus_health().await {
                        println!("Recovery successful!");
                        return Ok(());
                    } else {
                        println!("Bus still unhealthy after reset");
                    }
                }
                Err(e) => {
                    println!("Reset failed: {}", e);
                }
            }
            
            *self.attempt_count.write().await += 1;
        }
    }
    
    async fn calculate_backoff(&self, attempt: usize) -> Duration {
        let mut backoff = self.config.base_backoff;
        
        for _ in 0..attempt {
            backoff = backoff.saturating_mul(2);
            if backoff >= self.config.max_backoff {
                return self.config.max_backoff;
            }
        }
        
        backoff
    }
    
    async fn verify_bus_health(&self) -> bool {
        let check_duration = Duration::from_secs(2);
        let start = Instant::now();
        
        while start.elapsed() < check_duration {
            sleep(self.config.health_check_interval).await;
            
            let metrics = self.health_metrics.read().await;
            
            // Check if bus is stable
            if metrics.consecutive_successes >= 10 && metrics.error_rate < 0.01 {
                return true;
            }
        }
        
        false
    }
    
    /// Update health metrics from bus monitoring
    pub async fn update_health(&self, error_occurred: bool) {
        let mut metrics = self.health_metrics.write().await;
        
        if error_occurred {
            metrics.last_error = Some(Instant::now());
            metrics.consecutive_successes = 0;
            metrics.error_rate = (metrics.error_rate * 0.9) + 0.1;
        } else {
            metrics.consecutive_successes += 1;
            metrics.error_rate *= 0.95;
        }
    }
    
    pub async fn is_recovering(&self) -> bool {
        *self.is_recovering.read().await
    }
}

// Example usage
#[tokio::main]
async fn main() {
    let config = AsyncRecoveryConfig {
        base_backoff: Duration::from_secs(1),
        max_backoff: Duration::from_secs(30),
        max_attempts: 5,
        health_check_interval: Duration::from_millis(100),
    };
    
    let recovery_mgr = AsyncRecoveryManager::new(config);
    
    // Simulate bus-off event
    println!("Bus-off detected!");
    
    let result = recovery_mgr.recover_from_bus_off(|| async {
        // Simulate hardware reset
        sleep(Duration::from_millis(100)).await;
        Ok(())
    }).await;
    
    match result {
        Ok(()) => println!("Successfully recovered from bus-off"),
        Err(e) => println!("Recovery failed: {}", e),
    }
}
```

## Summary

**Bus-Off Recovery Strategies** are essential mechanisms for maintaining CAN network reliability and preventing faulty nodes from disrupting communication.

### Key Takeaways:

1. **Error Management**: CAN uses TEC/REC counters to track errors and transitions nodes through Error-Active → Error-Passive → Bus-Off states to isolate failures

2. **Recovery Approaches**:
   - **Automatic**: Fast but may cause repeated disruptions
   - **Exponential Backoff**: Balances recovery speed with network protection
   - **Supervised**: Application-controlled for mission-critical systems
   - **Conditional**: Requires diagnostics before rejoining

3. **Implementation Considerations**:
   - Track recovery attempts and success rates
   - Implement exponential backoff (base × 2^n, capped at max)
   - Verify bus health before declaring success
   - Log events for diagnostics and debugging
   - Consider hardware diagnostics for safety-critical systems

4. **Best Practices**:
   - Never allow infinite rapid recovery attempts
   - Implement proper state management
   - Use timeouts to prevent hanging
   - Provide manual override capabilities
   - Monitor and log recovery statistics
   - Consider network-wide impact of recovery strategy

5. **Language-Specific Strengths**:
   - **C/C++**: Direct hardware access, efficient for embedded systems
   - **Rust**: Type-safe state machines, memory safety, excellent async support

The choice of recovery strategy depends on application requirements, network criticality, and acceptable downtime. Safety-critical systems typically use supervised or conditional recovery, while less critical applications may use automatic recovery with backoff.