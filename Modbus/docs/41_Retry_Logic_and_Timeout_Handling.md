# Retry Logic and Timeout Handling in Modbus

## Overview

Retry logic and timeout handling are critical components of robust Modbus communication. Network issues, device busy states, and temporary failures are common in industrial environments. Implementing proper retry mechanisms with exponential backoff prevents system overload while maximizing successful communication.

## Core Concepts

**Timeout Handling** refers to detecting when a Modbus request hasn't received a response within an expected timeframe. Without timeouts, applications could hang indefinitely waiting for responses that will never arrive.

**Retry Logic** determines how the system responds to failures. Simple immediate retries can overwhelm struggling networks or devices, while exponential backoff gradually increases delays between attempts, giving the system time to recover.

**Exponential Backoff** is a strategy where retry delays increase exponentially (e.g., 100ms, 200ms, 400ms, 800ms). This prevents retry storms that could worsen network congestion.

**Maximum Retry Strategies** define when to give up. This includes limiting the number of attempts or setting an absolute timeout for the entire operation.

## Key Parameters

- **Initial Timeout**: Time to wait for first response (typically 500ms-2s)
- **Retry Count**: Maximum number of retry attempts (typically 3-5)
- **Backoff Factor**: Multiplier for exponential backoff (typically 2x)
- **Maximum Backoff**: Cap on retry delay (typically 10-30s)
- **Jitter**: Random variation to prevent synchronized retries

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

// Retry configuration structure
typedef struct {
    int max_retries;
    int initial_timeout_ms;
    int max_timeout_ms;
    float backoff_multiplier;
    int enable_jitter;
} retry_config_t;

// Retry state tracking
typedef struct {
    int attempt_count;
    int current_timeout_ms;
    struct timeval start_time;
} retry_state_t;

// Initialize retry configuration with defaults
void init_retry_config(retry_config_t *config) {
    config->max_retries = 3;
    config->initial_timeout_ms = 1000;
    config->max_timeout_ms = 10000;
    config->backoff_multiplier = 2.0;
    config->enable_jitter = 1;
}

// Initialize retry state
void init_retry_state(retry_state_t *state, int initial_timeout_ms) {
    state->attempt_count = 0;
    state->current_timeout_ms = initial_timeout_ms;
    gettimeofday(&state->start_time, NULL);
}

// Calculate next retry delay with exponential backoff
int calculate_backoff_delay(retry_state_t *state, retry_config_t *config) {
    int delay = state->current_timeout_ms;
    
    // Apply exponential backoff
    state->current_timeout_ms = (int)(state->current_timeout_ms * 
                                       config->backoff_multiplier);
    
    // Cap at maximum timeout
    if (state->current_timeout_ms > config->max_timeout_ms) {
        state->current_timeout_ms = config->max_timeout_ms;
    }
    
    // Add jitter (0-20% random variation)
    if (config->enable_jitter) {
        int jitter_range = delay / 5; // 20%
        int jitter = rand() % (jitter_range * 2) - jitter_range;
        delay += jitter;
    }
    
    return delay;
}

// Check if total operation has timed out
int is_total_timeout_exceeded(retry_state_t *state, int max_total_ms) {
    struct timeval now;
    gettimeofday(&now, NULL);
    
    long elapsed_ms = (now.tv_sec - state->start_time.tv_sec) * 1000 +
                      (now.tv_usec - state->start_time.tv_usec) / 1000;
    
    return elapsed_ms > max_total_ms;
}

// Simulated Modbus read function (replace with actual implementation)
int modbus_read_registers_with_timeout(int slave_id, int addr, 
                                       int count, uint16_t *dest,
                                       int timeout_ms) {
    // Simulate random failures for demonstration
    if (rand() % 3 == 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    
    // Simulate successful read
    usleep(timeout_ms * 100); // Simulate some delay
    for (int i = 0; i < count; i++) {
        dest[i] = 0x1234 + i;
    }
    return count;
}

// Modbus read with retry logic
int modbus_read_with_retry(int slave_id, int addr, int count, 
                           uint16_t *dest, retry_config_t *config) {
    retry_state_t state;
    init_retry_state(&state, config->initial_timeout_ms);
    
    int result = -1;
    int max_total_timeout = config->initial_timeout_ms * 
                            (1 << config->max_retries) * 2;
    
    while (state.attempt_count <= config->max_retries) {
        printf("Attempt %d/%d (timeout: %dms)\n", 
               state.attempt_count + 1,
               config->max_retries + 1,
               state.current_timeout_ms);
        
        // Check total timeout
        if (is_total_timeout_exceeded(&state, max_total_timeout)) {
            fprintf(stderr, "Total operation timeout exceeded\n");
            errno = ETIMEDOUT;
            break;
        }
        
        // Attempt the read
        result = modbus_read_registers_with_timeout(
            slave_id, addr, count, dest, state.current_timeout_ms
        );
        
        if (result >= 0) {
            printf("Success on attempt %d\n", state.attempt_count + 1);
            return result;
        }
        
        // Handle failure
        fprintf(stderr, "Attempt %d failed: %s\n", 
                state.attempt_count + 1, strerror(errno));
        
        state.attempt_count++;
        
        // Don't delay after last attempt
        if (state.attempt_count <= config->max_retries) {
            int delay = calculate_backoff_delay(&state, config);
            printf("Backing off for %dms...\n", delay);
            usleep(delay * 1000);
        }
    }
    
    fprintf(stderr, "All retry attempts exhausted\n");
    return -1;
}

// Example usage
int main() {
    srand(time(NULL));
    
    retry_config_t config;
    init_retry_config(&config);
    
    // Customize retry behavior
    config.max_retries = 4;
    config.initial_timeout_ms = 500;
    config.backoff_multiplier = 2.0;
    
    uint16_t registers[10];
    int result = modbus_read_with_retry(1, 0, 10, registers, &config);
    
    if (result > 0) {
        printf("\nSuccessfully read %d registers:\n", result);
        for (int i = 0; i < result; i++) {
            printf("Register %d: 0x%04X\n", i, registers[i]);
        }
    } else {
        printf("\nFailed to read registers after retries\n");
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};
use std::thread;
use rand::Rng;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ModbusError {
    #[error("Timeout waiting for response")]
    Timeout,
    #[error("Communication error: {0}")]
    Communication(String),
    #[error("Invalid response")]
    InvalidResponse,
    #[error("All retry attempts exhausted")]
    RetriesExhausted,
}

/// Configuration for retry behavior
#[derive(Debug, Clone)]
pub struct RetryConfig {
    pub max_retries: u32,
    pub initial_timeout: Duration,
    pub max_timeout: Duration,
    pub backoff_multiplier: f32,
    pub enable_jitter: bool,
}

impl Default for RetryConfig {
    fn default() -> Self {
        Self {
            max_retries: 3,
            initial_timeout: Duration::from_millis(1000),
            max_timeout: Duration::from_secs(10),
            backoff_multiplier: 2.0,
            enable_jitter: true,
        }
    }
}

/// Tracks state across retry attempts
struct RetryState {
    attempt_count: u32,
    current_timeout: Duration,
    start_time: Instant,
}

impl RetryState {
    fn new(initial_timeout: Duration) -> Self {
        Self {
            attempt_count: 0,
            current_timeout: initial_timeout,
            start_time: Instant::now(),
        }
    }

    fn calculate_backoff_delay(&mut self, config: &RetryConfig) -> Duration {
        let delay = self.current_timeout;
        
        // Apply exponential backoff
        let new_timeout = self.current_timeout.as_millis() as f32 
                         * config.backoff_multiplier;
        self.current_timeout = Duration::from_millis(new_timeout as u64);
        
        // Cap at maximum timeout
        if self.current_timeout > config.max_timeout {
            self.current_timeout = config.max_timeout;
        }
        
        // Add jitter (0-20% random variation)
        if config.enable_jitter {
            let mut rng = rand::thread_rng();
            let jitter_range = delay.as_millis() / 5; // 20%
            let jitter = rng.gen_range(0..jitter_range * 2) as i64 - jitter_range as i64;
            let jittered = delay.as_millis() as i64 + jitter;
            return Duration::from_millis(jittered.max(0) as u64);
        }
        
        delay
    }

    fn is_total_timeout_exceeded(&self, max_total: Duration) -> bool {
        self.start_time.elapsed() > max_total
    }
}

/// Simulated Modbus read function (replace with actual implementation)
fn modbus_read_registers(
    slave_id: u8,
    addr: u16,
    count: u16,
    timeout: Duration,
) -> Result<Vec<u16>, ModbusError> {
    // Simulate network delay
    thread::sleep(Duration::from_millis(50));
    
    // Simulate random failures for demonstration
    let mut rng = rand::thread_rng();
    if rng.gen_bool(0.4) {
        return Err(ModbusError::Timeout);
    }
    
    // Simulate successful read
    Ok((0..count).map(|i| 0x1234 + i).collect())
}

/// Read Modbus registers with retry logic
pub fn modbus_read_with_retry(
    slave_id: u8,
    addr: u16,
    count: u16,
    config: &RetryConfig,
) -> Result<Vec<u16>, ModbusError> {
    let mut state = RetryState::new(config.initial_timeout);
    
    // Calculate max total timeout
    let max_total_timeout = config.initial_timeout * (1 << config.max_retries) * 2;
    
    loop {
        println!(
            "Attempt {}/{} (timeout: {:?})",
            state.attempt_count + 1,
            config.max_retries + 1,
            state.current_timeout
        );
        
        // Check total timeout
        if state.is_total_timeout_exceeded(max_total_timeout) {
            eprintln!("Total operation timeout exceeded");
            return Err(ModbusError::RetriesExhausted);
        }
        
        // Attempt the read
        match modbus_read_registers(slave_id, addr, count, state.current_timeout) {
            Ok(data) => {
                println!("Success on attempt {}", state.attempt_count + 1);
                return Ok(data);
            }
            Err(e) => {
                eprintln!("Attempt {} failed: {}", state.attempt_count + 1, e);
                state.attempt_count += 1;
                
                if state.attempt_count > config.max_retries {
                    eprintln!("All retry attempts exhausted");
                    return Err(ModbusError::RetriesExhausted);
                }
                
                // Calculate and apply backoff delay
                let delay = state.calculate_backoff_delay(config);
                println!("Backing off for {:?}...", delay);
                thread::sleep(delay);
            }
        }
    }
}

/// Builder pattern for retry operations
pub struct RetryBuilder {
    config: RetryConfig,
}

impl RetryBuilder {
    pub fn new() -> Self {
        Self {
            config: RetryConfig::default(),
        }
    }

    pub fn max_retries(mut self, retries: u32) -> Self {
        self.config.max_retries = retries;
        self
    }

    pub fn initial_timeout(mut self, timeout: Duration) -> Self {
        self.config.initial_timeout = timeout;
        self
    }

    pub fn backoff_multiplier(mut self, multiplier: f32) -> Self {
        self.config.backoff_multiplier = multiplier;
        self
    }

    pub fn enable_jitter(mut self, enable: bool) -> Self {
        self.config.enable_jitter = enable;
        self
    }

    pub fn build(self) -> RetryConfig {
        self.config
    }
}

fn main() {
    // Example 1: Using default configuration
    println!("=== Example 1: Default Configuration ===\n");
    let config = RetryConfig::default();
    
    match modbus_read_with_retry(1, 0, 10, &config) {
        Ok(registers) => {
            println!("\nSuccessfully read {} registers:", registers.len());
            for (i, value) in registers.iter().enumerate() {
                println!("Register {}: 0x{:04X}", i, value);
            }
        }
        Err(e) => println!("\nFailed to read registers: {}", e),
    }

    // Example 2: Using builder pattern with custom configuration
    println!("\n\n=== Example 2: Custom Configuration ===\n");
    let custom_config = RetryBuilder::new()
        .max_retries(5)
        .initial_timeout(Duration::from_millis(500))
        .backoff_multiplier(1.5)
        .enable_jitter(true)
        .build();
    
    match modbus_read_with_retry(1, 100, 5, &custom_config) {
        Ok(registers) => {
            println!("\nSuccessfully read {} registers:", registers.len());
            for (i, value) in registers.iter().enumerate() {
                println!("Register {}: 0x{:04X}", i, value);
            }
        }
        Err(e) => println!("\nFailed to read registers: {}", e),
    }
}
```

## Summary

Retry logic and timeout handling are essential for reliable Modbus communication in real-world industrial environments. The key strategies include setting appropriate initial timeouts (typically 500ms-2s), implementing exponential backoff to prevent network flooding, limiting retry attempts (3-5 typically), adding jitter to prevent synchronized retries across multiple clients, and enforcing total operation timeouts.

The C/C++ implementation demonstrates low-level control with explicit state management and system calls, making it suitable for embedded systems and resource-constrained environments. The Rust implementation leverages modern language features like Result types, builder patterns, and strong type safety, providing more robust error handling and easier-to-maintain code.

Both implementations follow the same core algorithm: attempt the operation with an initial timeout, on failure, wait with exponential backoff, increment attempt counter and retry, give up after maximum attempts or total timeout exceeded. This approach balances reliability with system responsiveness, ensuring applications don't hang indefinitely while giving transient failures time to resolve.