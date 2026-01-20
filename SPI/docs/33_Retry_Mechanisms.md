# SPI Retry Mechanisms

## Overview

Retry mechanisms are essential for building robust SPI communication systems that can recover from transient failures. These failures can occur due to electrical noise, timing issues, peripheral busy states, or temporary communication disruptions. A well-designed retry mechanism attempts failed operations multiple times before reporting an error, significantly improving system reliability.

## Why Retry Mechanisms Matter

In embedded systems, SPI communication can fail for various reasons:
- **Transient electrical noise** affecting signal integrity
- **Timing violations** when peripherals are busy
- **Clock synchronization issues** during startup
- **Temporary peripheral unavailability**
- **Buffer overflow** in high-speed communications

Rather than immediately failing on a single error, retry mechanisms provide resilience by attempting the operation multiple times with appropriate delays.

## Key Concepts

### Retry Strategy Components

1. **Maximum Retry Count**: The number of attempts before giving up
2. **Delay Strategy**: Time to wait between attempts (fixed, exponential backoff, etc.)
3. **Error Classification**: Distinguishing retriable from permanent errors
4. **Timeout Handling**: Overall operation timeout independent of retries
5. **Success Verification**: Confirming the operation actually succeeded

### Common Retry Patterns

- **Immediate Retry**: No delay between attempts (for quick transients)
- **Fixed Delay**: Same delay between each retry
- **Exponential Backoff**: Increasing delays (avoid overwhelming busy peripherals)
- **Adaptive Retry**: Adjust strategy based on error types

## C Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Error codes
typedef enum {
    SPI_SUCCESS = 0,
    SPI_ERROR_TIMEOUT,
    SPI_ERROR_BUSY,
    SPI_ERROR_CRC,
    SPI_ERROR_NACK,
    SPI_ERROR_MAX_RETRIES
} spi_error_t;

// Retry configuration
typedef struct {
    uint8_t max_retries;
    uint16_t retry_delay_ms;
    bool use_exponential_backoff;
    uint16_t max_delay_ms;
} spi_retry_config_t;

// SPI retry context
typedef struct {
    spi_retry_config_t config;
    uint8_t retry_count;
    uint32_t last_error;
} spi_retry_ctx_t;

// Hardware-level SPI functions (platform-specific)
extern spi_error_t hal_spi_transfer(uint8_t *tx_data, uint8_t *rx_data, 
                                     size_t len);
extern void hal_delay_ms(uint16_t ms);

// Initialize retry context
void spi_retry_init(spi_retry_ctx_t *ctx, const spi_retry_config_t *config) {
    memcpy(&ctx->config, config, sizeof(spi_retry_config_t));
    ctx->retry_count = 0;
    ctx->last_error = SPI_SUCCESS;
}

// Determine if error is retriable
static bool is_retriable_error(spi_error_t error) {
    switch (error) {
        case SPI_ERROR_TIMEOUT:
        case SPI_ERROR_BUSY:
        case SPI_ERROR_CRC:
            return true;
        case SPI_ERROR_NACK:
        default:
            return false;
    }
}

// Calculate retry delay with optional exponential backoff
static uint16_t calculate_retry_delay(spi_retry_ctx_t *ctx) {
    uint16_t delay = ctx->config.retry_delay_ms;
    
    if (ctx->config.use_exponential_backoff && ctx->retry_count > 0) {
        // Exponential backoff: delay * 2^retry_count
        delay = ctx->config.retry_delay_ms << ctx->retry_count;
        
        // Cap at maximum delay
        if (delay > ctx->config.max_delay_ms) {
            delay = ctx->config.max_delay_ms;
        }
    }
    
    return delay;
}

// SPI transfer with retry mechanism
spi_error_t spi_transfer_with_retry(spi_retry_ctx_t *ctx,
                                    uint8_t *tx_data,
                                    uint8_t *rx_data,
                                    size_t len) {
    spi_error_t result;
    ctx->retry_count = 0;
    
    for (uint8_t attempt = 0; attempt <= ctx->config.max_retries; attempt++) {
        // Attempt the transfer
        result = hal_spi_transfer(tx_data, rx_data, len);
        
        if (result == SPI_SUCCESS) {
            return SPI_SUCCESS;
        }
        
        // Store last error
        ctx->last_error = result;
        ctx->retry_count = attempt;
        
        // Check if error is retriable
        if (!is_retriable_error(result)) {
            return result;  // Permanent error, don't retry
        }
        
        // Don't delay after the last attempt
        if (attempt < ctx->config.max_retries) {
            uint16_t delay = calculate_retry_delay(ctx);
            hal_delay_ms(delay);
        }
    }
    
    // Max retries exceeded
    return SPI_ERROR_MAX_RETRIES;
}

// Advanced: Transaction with verification
spi_error_t spi_transaction_verified(spi_retry_ctx_t *ctx,
                                     uint8_t *tx_data,
                                     uint8_t *rx_data,
                                     size_t len,
                                     bool (*verify_fn)(uint8_t*, size_t)) {
    spi_error_t result;
    
    for (uint8_t attempt = 0; attempt <= ctx->config.max_retries; attempt++) {
        result = hal_spi_transfer(tx_data, rx_data, len);
        
        if (result != SPI_SUCCESS) {
            ctx->last_error = result;
            ctx->retry_count = attempt;
            
            if (!is_retriable_error(result)) {
                return result;
            }
        } else {
            // Transfer succeeded, verify data
            if (verify_fn == NULL || verify_fn(rx_data, len)) {
                return SPI_SUCCESS;
            }
            // Verification failed, treat as CRC error
            ctx->last_error = SPI_ERROR_CRC;
            ctx->retry_count = attempt;
        }
        
        if (attempt < ctx->config.max_retries) {
            uint16_t delay = calculate_retry_delay(ctx);
            hal_delay_ms(delay);
        }
    }
    
    return SPI_ERROR_MAX_RETRIES;
}

// Example usage
void example_spi_retry(void) {
    // Configure retry mechanism
    spi_retry_config_t retry_config = {
        .max_retries = 3,
        .retry_delay_ms = 10,
        .use_exponential_backoff = true,
        .max_delay_ms = 100
    };
    
    spi_retry_ctx_t retry_ctx;
    spi_retry_init(&retry_ctx, &retry_config);
    
    // Perform transfer with automatic retry
    uint8_t tx_buffer[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rx_buffer[4];
    
    spi_error_t result = spi_transfer_with_retry(&retry_ctx, 
                                                  tx_buffer, 
                                                  rx_buffer, 
                                                  sizeof(tx_buffer));
    
    if (result == SPI_SUCCESS) {
        // Process received data
    } else if (result == SPI_ERROR_MAX_RETRIES) {
        // All retries exhausted
    } else {
        // Permanent error occurred
    }
}
```

## C++ Implementation

```cpp
#include <cstdint>
#include <chrono>
#include <thread>
#include <functional>
#include <optional>
#include <stdexcept>

namespace spi {

enum class Error {
    Success,
    Timeout,
    Busy,
    CrcError,
    Nack,
    MaxRetriesExceeded
};

// Retry policy interface
class RetryPolicy {
public:
    virtual ~RetryPolicy() = default;
    virtual bool shouldRetry(Error error, int attemptNumber) const = 0;
    virtual std::chrono::milliseconds getDelay(int attemptNumber) const = 0;
    virtual int maxAttempts() const = 0;
};

// Fixed delay retry policy
class FixedDelayPolicy : public RetryPolicy {
private:
    int max_attempts_;
    std::chrono::milliseconds delay_;
    
public:
    FixedDelayPolicy(int maxAttempts, std::chrono::milliseconds delay)
        : max_attempts_(maxAttempts), delay_(delay) {}
    
    bool shouldRetry(Error error, int attemptNumber) const override {
        if (attemptNumber >= max_attempts_) {
            return false;
        }
        
        // Determine retriable errors
        switch (error) {
            case Error::Timeout:
            case Error::Busy:
            case Error::CrcError:
                return true;
            default:
                return false;
        }
    }
    
    std::chrono::milliseconds getDelay(int attemptNumber) const override {
        return delay_;
    }
    
    int maxAttempts() const override {
        return max_attempts_;
    }
};

// Exponential backoff retry policy
class ExponentialBackoffPolicy : public RetryPolicy {
private:
    int max_attempts_;
    std::chrono::milliseconds initial_delay_;
    std::chrono::milliseconds max_delay_;
    
public:
    ExponentialBackoffPolicy(int maxAttempts,
                            std::chrono::milliseconds initialDelay,
                            std::chrono::milliseconds maxDelay)
        : max_attempts_(maxAttempts)
        , initial_delay_(initialDelay)
        , max_delay_(maxDelay) {}
    
    bool shouldRetry(Error error, int attemptNumber) const override {
        if (attemptNumber >= max_attempts_) {
            return false;
        }
        
        switch (error) {
            case Error::Timeout:
            case Error::Busy:
            case Error::CrcError:
                return true;
            default:
                return false;
        }
    }
    
    std::chrono::milliseconds getDelay(int attemptNumber) const override {
        auto delay = initial_delay_ * (1 << attemptNumber);
        return std::min(delay, max_delay_);
    }
    
    int maxAttempts() const override {
        return max_attempts_;
    }
};

// SPI Transfer with retry mechanism
template<typename T>
class RetryableTransfer {
private:
    const RetryPolicy& policy_;
    std::function<Error(T&)> operation_;
    
public:
    RetryableTransfer(const RetryPolicy& policy,
                     std::function<Error(T&)> operation)
        : policy_(policy), operation_(operation) {}
    
    struct Result {
        Error error;
        int attempts;
        std::optional<T> data;
    };
    
    Result execute() {
        Result result;
        result.attempts = 0;
        
        for (int attempt = 0; attempt < policy_.maxAttempts(); ++attempt) {
            result.attempts = attempt + 1;
            T data;
            
            result.error = operation_(data);
            
            if (result.error == Error::Success) {
                result.data = std::move(data);
                return result;
            }
            
            // Check if we should retry
            if (!policy_.shouldRetry(result.error, attempt)) {
                return result;
            }
            
            // Wait before retry (except on last attempt)
            if (attempt < policy_.maxAttempts() - 1) {
                auto delay = policy_.getDelay(attempt);
                std::this_thread::sleep_for(delay);
            }
        }
        
        result.error = Error::MaxRetriesExceeded;
        return result;
    }
};

// High-level SPI interface with retry support
class SpiDevice {
private:
    const RetryPolicy& retry_policy_;
    
    // Low-level transfer function (platform-specific)
    Error lowLevelTransfer(const uint8_t* tx, uint8_t* rx, size_t len) {
        // Implementation would call hardware-specific SPI functions
        // This is a placeholder
        return Error::Success;
    }
    
public:
    explicit SpiDevice(const RetryPolicy& policy)
        : retry_policy_(policy) {}
    
    struct TransferData {
        std::vector<uint8_t> tx_data;
        std::vector<uint8_t> rx_data;
    };
    
    auto transfer(std::vector<uint8_t> txData) {
        RetryableTransfer<TransferData> retryable(
            retry_policy_,
            [this, txData](TransferData& data) -> Error {
                data.tx_data = txData;
                data.rx_data.resize(txData.size());
                
                return lowLevelTransfer(
                    data.tx_data.data(),
                    data.rx_data.data(),
                    data.tx_data.size()
                );
            }
        );
        
        return retryable.execute();
    }
    
    // Transfer with custom verification
    template<typename VerifyFunc>
    auto transferVerified(std::vector<uint8_t> txData, VerifyFunc verify) {
        RetryableTransfer<TransferData> retryable(
            retry_policy_,
            [this, txData, verify](TransferData& data) -> Error {
                data.tx_data = txData;
                data.rx_data.resize(txData.size());
                
                Error err = lowLevelTransfer(
                    data.tx_data.data(),
                    data.rx_data.data(),
                    data.tx_data.size()
                );
                
                if (err != Error::Success) {
                    return err;
                }
                
                // Verify received data
                if (!verify(data.rx_data)) {
                    return Error::CrcError;
                }
                
                return Error::Success;
            }
        );
        
        return retryable.execute();
    }
};

} // namespace spi

// Example usage
void example() {
    using namespace std::chrono_literals;
    
    // Create exponential backoff policy
    spi::ExponentialBackoffPolicy policy(5, 10ms, 500ms);
    
    // Create SPI device with retry support
    spi::SpiDevice device(policy);
    
    // Perform transfer with automatic retry
    std::vector<uint8_t> tx_data = {0x01, 0x02, 0x03, 0x04};
    auto result = device.transfer(tx_data);
    
    if (result.error == spi::Error::Success && result.data) {
        // Process received data
        auto& rx_data = result.data->rx_data;
        // ... use rx_data ...
    } else {
        // Handle error
    }
    
    // Transfer with verification
    auto verified_result = device.transferVerified(
        tx_data,
        [](const std::vector<uint8_t>& data) {
            // Custom verification logic
            return data.size() == 4 && data[0] == 0xAA;
        }
    );
}
```

## Rust Implementation

```rust
use std::time::Duration;
use std::thread;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiError {
    Success,
    Timeout,
    Busy,
    CrcError,
    Nack,
    MaxRetriesExceeded,
}

// Retry policy trait
pub trait RetryPolicy {
    fn should_retry(&self, error: SpiError, attempt: usize) -> bool;
    fn get_delay(&self, attempt: usize) -> Duration;
    fn max_attempts(&self) -> usize;
}

// Fixed delay retry policy
pub struct FixedDelayPolicy {
    max_attempts: usize,
    delay: Duration,
}

impl FixedDelayPolicy {
    pub fn new(max_attempts: usize, delay: Duration) -> Self {
        Self { max_attempts, delay }
    }
}

impl RetryPolicy for FixedDelayPolicy {
    fn should_retry(&self, error: SpiError, attempt: usize) -> bool {
        if attempt >= self.max_attempts {
            return false;
        }
        
        matches!(error, SpiError::Timeout | SpiError::Busy | SpiError::CrcError)
    }
    
    fn get_delay(&self, _attempt: usize) -> Duration {
        self.delay
    }
    
    fn max_attempts(&self) -> usize {
        self.max_attempts
    }
}

// Exponential backoff retry policy
pub struct ExponentialBackoffPolicy {
    max_attempts: usize,
    initial_delay: Duration,
    max_delay: Duration,
}

impl ExponentialBackoffPolicy {
    pub fn new(max_attempts: usize, initial_delay: Duration, max_delay: Duration) -> Self {
        Self {
            max_attempts,
            initial_delay,
            max_delay,
        }
    }
}

impl RetryPolicy for ExponentialBackoffPolicy {
    fn should_retry(&self, error: SpiError, attempt: usize) -> bool {
        if attempt >= self.max_attempts {
            return false;
        }
        
        matches!(error, SpiError::Timeout | SpiError::Busy | SpiError::CrcError)
    }
    
    fn get_delay(&self, attempt: usize) -> Duration {
        let multiplier = 2_u32.pow(attempt as u32);
        let delay = self.initial_delay * multiplier;
        delay.min(self.max_delay)
    }
    
    fn max_attempts(&self) -> usize {
        self.max_attempts
    }
}

// Retry result
#[derive(Debug)]
pub struct RetryResult<T> {
    pub result: Result<T, SpiError>,
    pub attempts: usize,
}

// Generic retry executor
pub struct RetryExecutor<P: RetryPolicy> {
    policy: P,
}

impl<P: RetryPolicy> RetryExecutor<P> {
    pub fn new(policy: P) -> Self {
        Self { policy }
    }
    
    pub fn execute<T, F>(&self, mut operation: F) -> RetryResult<T>
    where
        F: FnMut() -> Result<T, SpiError>,
    {
        let mut attempts = 0;
        
        loop {
            attempts += 1;
            
            match operation() {
                Ok(value) => {
                    return RetryResult {
                        result: Ok(value),
                        attempts,
                    };
                }
                Err(error) => {
                    if !self.policy.should_retry(error, attempts - 1) {
                        return RetryResult {
                            result: Err(error),
                            attempts,
                        };
                    }
                    
                    if attempts < self.policy.max_attempts() {
                        let delay = self.policy.get_delay(attempts - 1);
                        thread::sleep(delay);
                    } else {
                        return RetryResult {
                            result: Err(SpiError::MaxRetriesExceeded),
                            attempts,
                        };
                    }
                }
            }
        }
    }
    
    // Execute with custom verification
    pub fn execute_verified<T, F, V>(
        &self,
        mut operation: F,
        verify: V,
    ) -> RetryResult<T>
    where
        F: FnMut() -> Result<T, SpiError>,
        V: Fn(&T) -> bool,
    {
        self.execute(|| {
            let result = operation()?;
            if verify(&result) {
                Ok(result)
            } else {
                Err(SpiError::CrcError)
            }
        })
    }
}

// SPI Device with retry support
pub struct SpiDevice<P: RetryPolicy> {
    executor: RetryExecutor<P>,
}

impl<P: RetryPolicy> SpiDevice<P> {
    pub fn new(policy: P) -> Self {
        Self {
            executor: RetryExecutor::new(policy),
        }
    }
    
    // Low-level transfer (platform-specific implementation)
    fn low_level_transfer(&self, tx_data: &[u8], rx_data: &mut [u8]) -> Result<(), SpiError> {
        // Platform-specific SPI transfer implementation
        // This is a placeholder
        Ok(())
    }
    
    pub fn transfer(&self, tx_data: &[u8]) -> RetryResult<Vec<u8>> {
        self.executor.execute(|| {
            let mut rx_data = vec![0u8; tx_data.len()];
            self.low_level_transfer(tx_data, &mut rx_data)?;
            Ok(rx_data)
        })
    }
    
    pub fn transfer_verified<F>(
        &self,
        tx_data: &[u8],
        verify: F,
    ) -> RetryResult<Vec<u8>>
    where
        F: Fn(&[u8]) -> bool,
    {
        self.executor.execute_verified(
            || {
                let mut rx_data = vec![0u8; tx_data.len()];
                self.low_level_transfer(tx_data, &mut rx_data)?;
                Ok(rx_data)
            },
            |data| verify(data),
        )
    }
}

// Example usage
fn example() {
    // Create exponential backoff policy
    let policy = ExponentialBackoffPolicy::new(
        5,
        Duration::from_millis(10),
        Duration::from_millis(500),
    );
    
    // Create SPI device with retry support
    let device = SpiDevice::new(policy);
    
    // Perform transfer with automatic retry
    let tx_data = vec![0x01, 0x02, 0x03, 0x04];
    let result = device.transfer(&tx_data);
    
    match result.result {
        Ok(rx_data) => {
            println!("Transfer succeeded after {} attempts", result.attempts);
            // Process received data
        }
        Err(error) => {
            println!("Transfer failed after {} attempts: {:?}", result.attempts, error);
        }
    }
    
    // Transfer with verification
    let verified_result = device.transfer_verified(&tx_data, |data| {
        // Custom verification logic
        data.len() == 4 && data[0] == 0xAA
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_fixed_delay_policy() {
        let policy = FixedDelayPolicy::new(3, Duration::from_millis(10));
        
        assert!(policy.should_retry(SpiError::Timeout, 0));
        assert!(policy.should_retry(SpiError::Busy, 1));
        assert!(!policy.should_retry(SpiError::Nack, 0));
        assert!(!policy.should_retry(SpiError::Timeout, 3));
    }
    
    #[test]
    fn test_exponential_backoff() {
        let policy = ExponentialBackoffPolicy::new(
            4,
            Duration::from_millis(10),
            Duration::from_millis(100),
        );
        
        assert_eq!(policy.get_delay(0), Duration::from_millis(10));
        assert_eq!(policy.get_delay(1), Duration::from_millis(20));
        assert_eq!(policy.get_delay(2), Duration::from_millis(40));
        assert_eq!(policy.get_delay(3), Duration::from_millis(80));
        assert_eq!(policy.get_delay(4), Duration::from_millis(100)); // Capped
    }
}
```

## Summary

**Retry mechanisms** are critical for building robust SPI communication systems that can tolerate transient failures. Key takeaways include:

- **Error Classification**: Distinguish between retriable errors (timeouts, busy states, CRC failures) and permanent errors (NACK, hardware failures) to avoid wasting retry attempts on unrecoverable conditions.

- **Retry Strategies**: Implement appropriate delay strategies—fixed delays work for simple cases, while exponential backoff prevents overwhelming busy peripherals and reduces collision probability in multi-master systems.

- **Verification Integration**: Combine retries with data verification (CRC, checksums, application-level validation) to ensure successful communication beyond mere transmission completion.

- **Resource Management**: Balance retry attempts against system constraints—too many retries can cause unacceptable latency, while too few reduce reliability. Typically 3-5 retries with 10-100ms delays provide good trade-offs.

- **Implementation Patterns**: Use policy-based designs (as shown in C++ and Rust) for flexibility, allowing different retry strategies for different peripherals or operating conditions without code duplication.

Properly implemented retry mechanisms transform brittle SPI communication into resilient, production-ready interfaces capable of operating reliably in noisy industrial environments and fault-prone conditions.