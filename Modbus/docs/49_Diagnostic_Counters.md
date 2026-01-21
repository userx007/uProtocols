# Modbus Diagnostic Counters

## Overview

Diagnostic counters are essential monitoring mechanisms in Modbus implementations that track various operational metrics, error conditions, and communication statistics. These counters provide visibility into system health, help identify communication issues, and enable predictive maintenance by tracking degradation patterns over time.

Modbus diagnostic counters typically monitor:
- **Communication errors** (CRC failures, timeouts, frame errors)
- **Message statistics** (successful transactions, retries)
- **Bus activity** (messages sent/received, busy conditions)
- **Device-specific conditions** (overruns, exceptions)

## Key Diagnostic Counter Types

### Standard Modbus Diagnostic Counters

The Modbus specification defines several standard diagnostic counters accessible via Function Code 08 (Diagnostics):

1. **Bus Message Counter** - Total messages detected on the network
2. **Bus Communication Error Counter** - Messages with CRC/LRC errors
3. **Server Exception Error Counter** - Number of exception responses generated
4. **Server Message Counter** - Messages addressed to this device
5. **Server No Response Counter** - Queries that produced no response
6. **Server NAK Counter** - Negative acknowledge count
7. **Server Busy Counter** - Server busy exception count
8. **Bus Character Overrun Counter** - Character overrun errors

### Additional Implementation-Specific Counters

- **Timeout counters** - Request/response timeouts
- **Retry counters** - Transmission retry attempts
- **Queue depth metrics** - Message queue statistics
- **Latency measurements** - Response time tracking

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

// Diagnostic counter structure
typedef struct {
    // Standard Modbus diagnostic counters
    uint16_t bus_message_count;           // Total messages on bus
    uint16_t bus_comm_error_count;        // CRC/parity errors
    uint16_t server_exception_count;      // Exception responses sent
    uint16_t server_message_count;        // Messages addressed to device
    uint16_t server_no_response_count;    // No response sent
    uint16_t server_nak_count;            // NAK responses
    uint16_t server_busy_count;           // Busy exceptions
    uint16_t bus_char_overrun_count;      // Character overruns
    
    // Extended diagnostic counters
    uint32_t total_requests;              // Total requests processed
    uint32_t successful_responses;        // Successful transactions
    uint32_t timeout_count;               // Timeout errors
    uint32_t retry_count;                 // Retry attempts
    uint32_t frame_error_count;           // Framing errors
    uint32_t invalid_function_count;      // Invalid function codes
    
    // Performance metrics
    uint64_t total_response_time_ms;      // Cumulative response time
    uint32_t max_response_time_ms;        // Peak response time
    uint32_t min_response_time_ms;        // Minimum response time
    
    // Health indicators
    time_t last_successful_comm;          // Last successful communication
    time_t last_error_time;               // Last error timestamp
    uint8_t consecutive_errors;           // Consecutive error count
} modbus_diagnostic_counters_t;

// Diagnostic counter manager
typedef struct {
    modbus_diagnostic_counters_t counters;
    bool enabled;
    uint32_t reset_count;                 // Number of resets performed
    time_t last_reset_time;               // Last counter reset time
} modbus_diagnostics_t;

// Initialize diagnostics
void modbus_diagnostics_init(modbus_diagnostics_t *diag) {
    memset(diag, 0, sizeof(modbus_diagnostics_t));
    diag->enabled = true;
    diag->counters.min_response_time_ms = UINT32_MAX;
    diag->last_reset_time = time(NULL);
}

// Update message received counter
void modbus_diag_message_received(modbus_diagnostics_t *diag, bool addressed_to_us) {
    if (!diag->enabled) return;
    
    diag->counters.bus_message_count++;
    if (addressed_to_us) {
        diag->counters.server_message_count++;
        diag->counters.total_requests++;
    }
}

// Record communication error
void modbus_diag_comm_error(modbus_diagnostics_t *diag, const char *error_type) {
    if (!diag->enabled) return;
    
    diag->counters.bus_comm_error_count++;
    diag->counters.consecutive_errors++;
    diag->last_error_time = time(NULL);
    
    // Categorize error type
    if (strcmp(error_type, "CRC") == 0 || strcmp(error_type, "LRC") == 0) {
        // CRC/LRC errors already counted in bus_comm_error_count
    } else if (strcmp(error_type, "TIMEOUT") == 0) {
        diag->counters.timeout_count++;
    } else if (strcmp(error_type, "FRAME") == 0) {
        diag->counters.frame_error_count++;
    } else if (strcmp(error_type, "OVERRUN") == 0) {
        diag->counters.bus_char_overrun_count++;
    }
}

// Record exception response
void modbus_diag_exception(modbus_diagnostics_t *diag, uint8_t exception_code) {
    if (!diag->enabled) return;
    
    diag->counters.server_exception_count++;
    
    if (exception_code == 0x06) {  // Server busy
        diag->counters.server_busy_count++;
    } else if (exception_code == 0x07) {  // NAK
        diag->counters.server_nak_count++;
    } else if (exception_code == 0x01) {  // Illegal function
        diag->counters.invalid_function_count++;
    }
}

// Record successful transaction
void modbus_diag_success(modbus_diagnostics_t *diag, uint32_t response_time_ms) {
    if (!diag->enabled) return;
    
    diag->counters.successful_responses++;
    diag->counters.consecutive_errors = 0;
    diag->last_successful_comm = time(NULL);
    
    // Update response time statistics
    diag->counters.total_response_time_ms += response_time_ms;
    
    if (response_time_ms > diag->counters.max_response_time_ms) {
        diag->counters.max_response_time_ms = response_time_ms;
    }
    
    if (response_time_ms < diag->counters.min_response_time_ms) {
        diag->counters.min_response_time_ms = response_time_ms;
    }
}

// Calculate average response time
double modbus_diag_avg_response_time(const modbus_diagnostics_t *diag) {
    if (diag->counters.successful_responses == 0) {
        return 0.0;
    }
    return (double)diag->counters.total_response_time_ms / 
           diag->counters.successful_responses;
}

// Calculate error rate
double modbus_diag_error_rate(const modbus_diagnostics_t *diag) {
    uint32_t total = diag->counters.total_requests;
    if (total == 0) return 0.0;
    
    uint32_t errors = diag->counters.bus_comm_error_count + 
                      diag->counters.timeout_count +
                      diag->counters.server_exception_count;
    
    return (double)errors / total * 100.0;
}

// Check health status
typedef enum {
    HEALTH_GOOD = 0,
    HEALTH_WARNING = 1,
    HEALTH_CRITICAL = 2,
    HEALTH_UNKNOWN = 3
} health_status_t;

health_status_t modbus_diag_health_status(const modbus_diagnostics_t *diag) {
    double error_rate = modbus_diag_error_rate(diag);
    time_t now = time(NULL);
    double time_since_last_comm = difftime(now, diag->last_successful_comm);
    
    // Critical conditions
    if (diag->counters.consecutive_errors > 10 || error_rate > 50.0) {
        return HEALTH_CRITICAL;
    }
    
    // Warning conditions
    if (diag->counters.consecutive_errors > 5 || 
        error_rate > 10.0 || 
        time_since_last_comm > 60.0) {
        return HEALTH_WARNING;
    }
    
    // Good health
    if (diag->counters.successful_responses > 0) {
        return HEALTH_GOOD;
    }
    
    return HEALTH_UNKNOWN;
}

// Reset diagnostic counters
void modbus_diagnostics_reset(modbus_diagnostics_t *diag) {
    memset(&diag->counters, 0, sizeof(modbus_diagnostic_counters_t));
    diag->counters.min_response_time_ms = UINT32_MAX;
    diag->reset_count++;
    diag->last_reset_time = time(NULL);
}

// Print diagnostic report
void modbus_diagnostics_report(const modbus_diagnostics_t *diag) {
    printf("=== Modbus Diagnostic Report ===\n");
    printf("Status: ");
    switch (modbus_diag_health_status(diag)) {
        case HEALTH_GOOD: printf("GOOD\n"); break;
        case HEALTH_WARNING: printf("WARNING\n"); break;
        case HEALTH_CRITICAL: printf("CRITICAL\n"); break;
        default: printf("UNKNOWN\n"); break;
    }
    
    printf("\nMessage Statistics:\n");
    printf("  Total Bus Messages: %u\n", diag->counters.bus_message_count);
    printf("  Server Messages: %u\n", diag->counters.server_message_count);
    printf("  Successful Responses: %u\n", diag->counters.successful_responses);
    
    printf("\nError Counters:\n");
    printf("  Communication Errors: %u\n", diag->counters.bus_comm_error_count);
    printf("  Timeouts: %u\n", diag->counters.timeout_count);
    printf("  Exceptions: %u\n", diag->counters.server_exception_count);
    printf("  Frame Errors: %u\n", diag->counters.frame_error_count);
    printf("  Error Rate: %.2f%%\n", modbus_diag_error_rate(diag));
    
    printf("\nPerformance:\n");
    printf("  Avg Response Time: %.2f ms\n", modbus_diag_avg_response_time(diag));
    printf("  Min Response Time: %u ms\n", diag->counters.min_response_time_ms);
    printf("  Max Response Time: %u ms\n", diag->counters.max_response_time_ms);
}
```

## Rust Implementation

```rust
use std::time::{Duration, Instant, SystemTime};
use std::sync::{Arc, Mutex};

/// Standard Modbus diagnostic counters
#[derive(Debug, Clone, Default)]
pub struct ModbusDiagnosticCounters {
    // Standard counters
    pub bus_message_count: u16,
    pub bus_comm_error_count: u16,
    pub server_exception_count: u16,
    pub server_message_count: u16,
    pub server_no_response_count: u16,
    pub server_nak_count: u16,
    pub server_busy_count: u16,
    pub bus_char_overrun_count: u16,
    
    // Extended counters
    pub total_requests: u32,
    pub successful_responses: u32,
    pub timeout_count: u32,
    pub retry_count: u32,
    pub frame_error_count: u32,
    pub invalid_function_count: u32,
    
    // Performance metrics
    pub total_response_time: Duration,
    pub max_response_time: Duration,
    pub min_response_time: Duration,
    
    // Health indicators
    pub last_successful_comm: Option<SystemTime>,
    pub last_error_time: Option<SystemTime>,
    pub consecutive_errors: u8,
}

/// Health status enumeration
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealthStatus {
    Good,
    Warning,
    Critical,
    Unknown,
}

/// Diagnostic counter manager
pub struct ModbusDiagnostics {
    counters: Arc<Mutex<ModbusDiagnosticCounters>>,
    enabled: bool,
    reset_count: u32,
    last_reset_time: SystemTime,
}

impl ModbusDiagnostics {
    /// Create new diagnostics instance
    pub fn new() -> Self {
        Self {
            counters: Arc::new(Mutex::new(ModbusDiagnosticCounters {
                min_response_time: Duration::from_secs(u64::MAX),
                ..Default::default()
            })),
            enabled: true,
            reset_count: 0,
            last_reset_time: SystemTime::now(),
        }
    }
    
    /// Record a received message
    pub fn message_received(&self, addressed_to_us: bool) {
        if !self.enabled {
            return;
        }
        
        if let Ok(mut counters) = self.counters.lock() {
            counters.bus_message_count = counters.bus_message_count.wrapping_add(1);
            if addressed_to_us {
                counters.server_message_count = 
                    counters.server_message_count.wrapping_add(1);
                counters.total_requests += 1;
            }
        }
    }
    
    /// Record a communication error
    pub fn comm_error(&self, error_type: &str) {
        if !self.enabled {
            return;
        }
        
        if let Ok(mut counters) = self.counters.lock() {
            counters.bus_comm_error_count = 
                counters.bus_comm_error_count.wrapping_add(1);
            counters.consecutive_errors = counters.consecutive_errors.saturating_add(1);
            counters.last_error_time = Some(SystemTime::now());
            
            match error_type {
                "TIMEOUT" => counters.timeout_count += 1,
                "FRAME" => counters.frame_error_count += 1,
                "OVERRUN" => counters.bus_char_overrun_count = 
                    counters.bus_char_overrun_count.wrapping_add(1),
                _ => {}
            }
        }
    }
    
    /// Record an exception response
    pub fn exception(&self, exception_code: u8) {
        if !self.enabled {
            return;
        }
        
        if let Ok(mut counters) = self.counters.lock() {
            counters.server_exception_count = 
                counters.server_exception_count.wrapping_add(1);
            
            match exception_code {
                0x06 => counters.server_busy_count = 
                    counters.server_busy_count.wrapping_add(1),
                0x07 => counters.server_nak_count = 
                    counters.server_nak_count.wrapping_add(1),
                0x01 => counters.invalid_function_count += 1,
                _ => {}
            }
        }
    }
    
    /// Record a successful transaction
    pub fn success(&self, response_time: Duration) {
        if !self.enabled {
            return;
        }
        
        if let Ok(mut counters) = self.counters.lock() {
            counters.successful_responses += 1;
            counters.consecutive_errors = 0;
            counters.last_successful_comm = Some(SystemTime::now());
            
            counters.total_response_time += response_time;
            
            if response_time > counters.max_response_time {
                counters.max_response_time = response_time;
            }
            
            if response_time < counters.min_response_time {
                counters.min_response_time = response_time;
            }
        }
    }
    
    /// Record a retry attempt
    pub fn retry(&self) {
        if !self.enabled {
            return;
        }
        
        if let Ok(mut counters) = self.counters.lock() {
            counters.retry_count += 1;
        }
    }
    
    /// Calculate average response time
    pub fn avg_response_time(&self) -> Option<Duration> {
        let counters = self.counters.lock().ok()?;
        
        if counters.successful_responses == 0 {
            return None;
        }
        
        Some(counters.total_response_time / counters.successful_responses)
    }
    
    /// Calculate error rate as percentage
    pub fn error_rate(&self) -> f64 {
        let counters = match self.counters.lock() {
            Ok(c) => c,
            Err(_) => return 0.0,
        };
        
        if counters.total_requests == 0 {
            return 0.0;
        }
        
        let errors = counters.bus_comm_error_count as u32 +
                     counters.timeout_count +
                     counters.server_exception_count as u32;
        
        (errors as f64 / counters.total_requests as f64) * 100.0
    }
    
    /// Get current health status
    pub fn health_status(&self) -> HealthStatus {
        let counters = match self.counters.lock() {
            Ok(c) => c,
            Err(_) => return HealthStatus::Unknown,
        };
        
        let error_rate = self.error_rate();
        let time_since_comm = counters.last_successful_comm
            .and_then(|t| SystemTime::now().duration_since(t).ok())
            .unwrap_or(Duration::from_secs(u64::MAX));
        
        // Critical conditions
        if counters.consecutive_errors > 10 || error_rate > 50.0 {
            return HealthStatus::Critical;
        }
        
        // Warning conditions
        if counters.consecutive_errors > 5 || 
           error_rate > 10.0 || 
           time_since_comm > Duration::from_secs(60) {
            return HealthStatus::Warning;
        }
        
        // Good health
        if counters.successful_responses > 0 {
            return HealthStatus::Good;
        }
        
        HealthStatus::Unknown
    }
    
    /// Reset all counters
    pub fn reset(&mut self) {
        if let Ok(mut counters) = self.counters.lock() {
            *counters = ModbusDiagnosticCounters {
                min_response_time: Duration::from_secs(u64::MAX),
                ..Default::default()
            };
        }
        self.reset_count += 1;
        self.last_reset_time = SystemTime::now();
    }
    
    /// Get a snapshot of current counters
    pub fn snapshot(&self) -> Option<ModbusDiagnosticCounters> {
        self.counters.lock().ok().map(|c| c.clone())
    }
    
    /// Generate diagnostic report
    pub fn report(&self) -> String {
        let counters = match self.counters.lock() {
            Ok(c) => c.clone(),
            Err(_) => return "Failed to lock counters".to_string(),
        };
        
        format!(
            "=== Modbus Diagnostic Report ===\n\
             Status: {:?}\n\n\
             Message Statistics:\n\
             Total Bus Messages: {}\n\
             Server Messages: {}\n\
             Successful Responses: {}\n\n\
             Error Counters:\n\
             Communication Errors: {}\n\
             Timeouts: {}\n\
             Exceptions: {}\n\
             Frame Errors: {}\n\
             Error Rate: {:.2}%\n\n\
             Performance:\n\
             Avg Response Time: {:?}\n\
             Min Response Time: {:?}\n\
             Max Response Time: {:?}\n",
            self.health_status(),
            counters.bus_message_count,
            counters.server_message_count,
            counters.successful_responses,
            counters.bus_comm_error_count,
            counters.timeout_count,
            counters.server_exception_count,
            counters.frame_error_count,
            self.error_rate(),
            self.avg_response_time(),
            counters.min_response_time,
            counters.max_response_time
        )
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_diagnostic_counters() {
        let diag = ModbusDiagnostics::new();
        
        // Simulate successful transactions
        diag.message_received(true);
        diag.success(Duration::from_millis(25));
        
        diag.message_received(true);
        diag.success(Duration::from_millis(30));
        
        // Simulate errors
        diag.message_received(true);
        diag.comm_error("TIMEOUT");
        
        // Check statistics
        assert_eq!(diag.error_rate(), 33.33333333333333);
        assert_eq!(diag.health_status(), HealthStatus::Warning);
        
        println!("{}", diag.report());
    }
}
```

## Summary

Modbus diagnostic counters provide essential visibility into communication health and system performance. By tracking message statistics, error conditions, and performance metrics, these counters enable:

**Key Benefits:**
- **Proactive monitoring** - Detect issues before they cause failures
- **Performance optimization** - Identify bottlenecks and slow devices
- **Troubleshooting** - Pinpoint communication problems quickly
- **Predictive maintenance** - Track degradation patterns over time
- **Compliance** - Meet industrial monitoring requirements

**Implementation Considerations:**
- Use atomic operations or mutexes for thread-safe counter updates
- Implement counter rollover handling (especially for 16-bit counters)
- Balance counter granularity with memory/performance overhead
- Provide both raw counters and calculated metrics (rates, averages)
- Consider persistent storage for long-term trend analysis
- Implement health thresholds appropriate for your application

**Best Practices:**
- Reset counters only when necessary (preserve historical data)
- Export diagnostic data in standard formats (JSON, CSV)
- Integrate with monitoring systems (SCADA, logging infrastructure)
- Track both device-level and system-level statistics
- Document counter meanings and thresholds clearly

Diagnostic counters transform Modbus systems from "black boxes" into observable, maintainable systems with clear health indicators and actionable metrics for operations teams.