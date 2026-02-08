# UART Baud Rate Configuration

## Detailed Description

Baud rate configuration is a fundamental aspect of UART (Universal Asynchronous Receiver/Transmitter) communication that determines the speed at which data is transmitted between devices. The baud rate represents the number of signal or symbol changes per second, and in most UART implementations, it directly corresponds to bits per second (bps).

### Core Concepts

**Baud Rate Fundamentals:**
The baud rate must be configured identically on both transmitting and receiving devices for successful communication. Common standard baud rates include 9600, 19200, 38400, 57600, 115200, 230400, 460800, and 921600 bps. The choice of baud rate depends on factors such as the required data throughput, cable length, noise environment, and the capabilities of both communicating devices.

**Clock Division:**
UART hardware generates the baud rate by dividing down a system or peripheral clock. The fundamental equation is:

```
Baud Rate = Clock Frequency / (Divisor × Oversampling)
```

Where the divisor is a programmable value written to baud rate registers, and oversampling (typically 16x or 8x) helps with accurate bit sampling in the middle of each bit period.

**Baud Rate Tolerance:**
Since real-world clock frequencies may not divide evenly to produce exact baud rates, there's always some error. The maximum tolerable error depends on the UART frame format but generally should be kept below 2-3% for reliable communication. This becomes more critical with longer data frames (more bits per frame means accumulated error).

### Register Configuration

Most microcontrollers use one or more registers to configure the baud rate:

1. **Baud Rate Registers (BRR):** Store the divisor value
2. **Fractional Dividers:** Some UARTs support fractional division for more accurate baud rates
3. **Clock Source Selection:** Choosing which clock feeds the UART peripheral

The calculation typically involves:
1. Determining the desired baud rate
2. Knowing the peripheral clock frequency
3. Calculating the divisor value
4. Accounting for oversampling
5. Verifying the error is within tolerance

## C/C++ Code Examples

### Example 1: Basic Baud Rate Configuration (STM32-style)

```c
#include <stdint.h>

// UART Register Structure
typedef struct {
    volatile uint32_t SR;   // Status register
    volatile uint32_t DR;   // Data register
    volatile uint32_t BRR;  // Baud rate register
    volatile uint32_t CR1;  // Control register 1
    volatile uint32_t CR2;  // Control register 2
    volatile uint32_t CR3;  // Control register 3
} UART_TypeDef;

#define UART1 ((UART_TypeDef*)0x40011000)
#define APB2_CLOCK 72000000  // 72 MHz

/**
 * Calculate baud rate divisor
 * @param clock_freq: Peripheral clock frequency in Hz
 * @param baud_rate: Desired baud rate
 * @return Divisor value for BRR register
 */
uint32_t calculate_baud_divisor(uint32_t clock_freq, uint32_t baud_rate) {
    // For 16x oversampling: Divisor = Clock / (16 * BaudRate)
    return (clock_freq + (baud_rate * 8)) / (baud_rate * 16);
}

/**
 * Calculate actual baud rate and error percentage
 */
float calculate_baud_error(uint32_t clock_freq, uint32_t divisor, 
                          uint32_t desired_baud) {
    uint32_t actual_baud = clock_freq / (divisor * 16);
    float error = ((float)(actual_baud - desired_baud) / desired_baud) * 100.0f;
    return error;
}

/**
 * Configure UART baud rate
 */
void uart_set_baudrate(UART_TypeDef *uart, uint32_t clock_freq, 
                      uint32_t baud_rate) {
    uint32_t divisor = calculate_baud_divisor(clock_freq, baud_rate);
    
    // Verify error is acceptable (< 2%)
    float error = calculate_baud_error(clock_freq, divisor, baud_rate);
    if (error > 2.0f || error < -2.0f) {
        // Handle error - perhaps log warning or choose different clock
    }
    
    // Set baud rate register
    uart->BRR = divisor;
}

// Usage example
void setup_uart(void) {
    uart_set_baudrate(UART1, APB2_CLOCK, 115200);
}
```

### Example 2: Fractional Baud Rate with Error Checking

```c
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t mantissa;
    uint32_t fraction;
    float error_percent;
    uint32_t actual_baud;
} BaudConfig_t;

/**
 * Calculate fractional baud rate divisor
 * Supports 4-bit fractional part for more accurate rates
 */
BaudConfig_t calculate_fractional_baud(uint32_t clock_freq, 
                                       uint32_t desired_baud,
                                       uint8_t oversampling) {
    BaudConfig_t config = {0};
    
    // USARTDIV = Clock / (Oversampling * BaudRate)
    float usartdiv = (float)clock_freq / (oversampling * desired_baud);
    
    // Split into mantissa and fraction
    config.mantissa = (uint32_t)usartdiv;
    float fraction_part = usartdiv - config.mantissa;
    config.fraction = (uint32_t)(fraction_part * 16.0f + 0.5f); // Round
    
    // Handle fraction overflow
    if (config.fraction >= 16) {
        config.mantissa++;
        config.fraction = 0;
    }
    
    // Calculate actual baud rate and error
    float actual_div = config.mantissa + (config.fraction / 16.0f);
    config.actual_baud = (uint32_t)(clock_freq / (oversampling * actual_div));
    config.error_percent = ((float)(config.actual_baud - desired_baud) / 
                           desired_baud) * 100.0f;
    
    return config;
}

/**
 * Comprehensive baud rate setup with validation
 */
bool uart_configure_baud_safe(UART_TypeDef *uart, uint32_t clock_freq,
                              uint32_t desired_baud) {
    const float MAX_ERROR = 2.0f; // 2% maximum error
    
    // Try 16x oversampling first
    BaudConfig_t config = calculate_fractional_baud(clock_freq, 
                                                    desired_baud, 16);
    
    // If error too high, try 8x oversampling (if supported)
    if (config.error_percent > MAX_ERROR || 
        config.error_percent < -MAX_ERROR) {
        config = calculate_fractional_baud(clock_freq, desired_baud, 8);
        
        if (config.error_percent > MAX_ERROR || 
            config.error_percent < -MAX_ERROR) {
            return false; // Cannot achieve acceptable baud rate
        }
        
        // Enable 8x oversampling in CR1
        uart->CR1 |= (1 << 15); // OVER8 bit
    }
    
    // Construct BRR value: Mantissa[15:4], Fraction[3:0]
    uint32_t brr_value = (config.mantissa << 4) | (config.fraction & 0x0F);
    uart->BRR = brr_value;
    
    return true;
}
```

### Example 3: Multi-Platform Baud Rate Configuration

```cpp
#include <stdint.h>
#include <array>

class UARTBaudConfig {
private:
    static constexpr std::array<uint32_t, 9> STANDARD_BAUDS = {
        9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000
    };
    
    uint32_t clock_freq_;
    uint8_t oversampling_;
    
public:
    struct BaudResult {
        uint32_t brr_value;
        float error_percent;
        uint32_t actual_baud;
        bool valid;
    };
    
    UARTBaudConfig(uint32_t clock_freq, uint8_t oversampling = 16)
        : clock_freq_(clock_freq), oversampling_(oversampling) {}
    
    /**
     * Find the best achievable baud rate near the target
     */
    BaudResult calculate_best_baud(uint32_t target_baud, 
                                   float max_error = 2.0f) {
        BaudResult result = {0, 0.0f, 0, false};
        
        // Calculate divisor with fractional part
        float divisor = static_cast<float>(clock_freq_) / 
                       (oversampling_ * target_baud);
        
        uint32_t mantissa = static_cast<uint32_t>(divisor);
        float frac_part = divisor - mantissa;
        uint32_t fraction = static_cast<uint32_t>(frac_part * 16.0f + 0.5f);
        
        // Handle overflow
        if (fraction >= 16) {
            mantissa++;
            fraction = 0;
        }
        
        // Construct BRR
        result.brr_value = (mantissa << 4) | (fraction & 0x0F);
        
        // Calculate actual baud and error
        float actual_div = mantissa + (fraction / 16.0f);
        result.actual_baud = static_cast<uint32_t>(
            clock_freq_ / (oversampling_ * actual_div)
        );
        
        result.error_percent = 
            (static_cast<float>(result.actual_baud - target_baud) / 
             target_baud) * 100.0f;
        
        result.valid = (result.error_percent <= max_error) && 
                      (result.error_percent >= -max_error);
        
        return result;
    }
    
    /**
     * Find closest standard baud rate that can be achieved
     */
    BaudResult find_closest_standard_baud(uint32_t target_baud) {
        BaudResult best_result = {0, 100.0f, 0, false};
        
        for (uint32_t std_baud : STANDARD_BAUDS) {
            BaudResult current = calculate_best_baud(std_baud, 5.0f);
            
            if (current.valid && 
                std::abs(current.error_percent) < 
                std::abs(best_result.error_percent)) {
                best_result = current;
            }
        }
        
        return best_result;
    }
};

// Usage example
void configure_uart_cpp(void) {
    UARTBaudConfig baud_calc(72000000); // 72 MHz clock
    
    auto result = baud_calc.calculate_best_baud(115200);
    
    if (result.valid) {
        UART1->BRR = result.brr_value;
        // Log: Actual baud: result.actual_baud, Error: result.error_percent%
    }
}
```

## Rust Code Examples

### Example 1: Safe Baud Rate Configuration

```rust
#![no_std]

/// UART peripheral register block
#[repr(C)]
pub struct UartRegisters {
    pub sr: u32,   // Status register
    pub dr: u32,   // Data register
    pub brr: u32,  // Baud rate register
    pub cr1: u32,  // Control register 1
    pub cr2: u32,  // Control register 2
    pub cr3: u32,  // Control register 3
}

/// Baud rate configuration result
#[derive(Debug, Clone, Copy)]
pub struct BaudConfig {
    pub brr_value: u32,
    pub actual_baud: u32,
    pub error_percent: f32,
}

/// Error types for baud rate configuration
#[derive(Debug)]
pub enum BaudError {
    ErrorTooHigh,
    DivisorOutOfRange,
    InvalidOversampling,
}

/// Calculate baud rate divisor with error checking
pub fn calculate_baud_config(
    clock_freq: u32,
    desired_baud: u32,
    oversampling: u8,
) -> Result<BaudConfig, BaudError> {
    // Validate oversampling
    if oversampling != 8 && oversampling != 16 {
        return Err(BaudError::InvalidOversampling);
    }
    
    // Calculate divisor as float for precision
    let usartdiv = clock_freq as f32 / (oversampling as f32 * desired_baud as f32);
    
    // Split into mantissa and fraction
    let mantissa = usartdiv as u32;
    let fraction_part = usartdiv - mantissa as f32;
    let mut fraction = (fraction_part * 16.0 + 0.5) as u32;
    
    // Handle fraction overflow
    let final_mantissa = if fraction >= 16 {
        fraction = 0;
        mantissa + 1
    } else {
        mantissa
    };
    
    // Check divisor range (typical limits)
    if final_mantissa == 0 || final_mantissa > 0xFFF {
        return Err(BaudError::DivisorOutOfRange);
    }
    
    // Construct BRR value
    let brr_value = (final_mantissa << 4) | (fraction & 0x0F);
    
    // Calculate actual baud rate
    let actual_div = final_mantissa as f32 + (fraction as f32 / 16.0);
    let actual_baud = (clock_freq as f32 / 
                      (oversampling as f32 * actual_div)) as u32;
    
    // Calculate error percentage
    let error_percent = ((actual_baud as f32 - desired_baud as f32) / 
                        desired_baud as f32) * 100.0;
    
    // Check if error is acceptable (< 2%)
    if error_percent.abs() > 2.0 {
        return Err(BaudError::ErrorTooHigh);
    }
    
    Ok(BaudConfig {
        brr_value,
        actual_baud,
        error_percent,
    })
}

/// Configure UART baud rate with automatic oversampling selection
pub fn configure_uart_baud(
    uart: &mut UartRegisters,
    clock_freq: u32,
    desired_baud: u32,
) -> Result<BaudConfig, BaudError> {
    // Try 16x oversampling first
    match calculate_baud_config(clock_freq, desired_baud, 16) {
        Ok(config) => {
            uart.brr = config.brr_value;
            Ok(config)
        }
        Err(BaudError::ErrorTooHigh) => {
            // Try 8x oversampling
            let config = calculate_baud_config(clock_freq, desired_baud, 8)?;
            uart.brr = config.brr_value;
            uart.cr1 |= 1 << 15; // Set OVER8 bit
            Ok(config)
        }
        Err(e) => Err(e),
    }
}
```

### Example 2: Type-Safe Baud Rate with Const Generics

```rust
/// Standard baud rates as const generics
pub trait BaudRate {
    const RATE: u32;
}

pub struct Baud9600;
impl BaudRate for Baud9600 { const RATE: u32 = 9600; }

pub struct Baud115200;
impl BaudRate for Baud115200 { const RATE: u32 = 115200; }

pub struct Baud921600;
impl BaudRate for Baud921600 { const RATE: u32 = 921600; }

/// UART peripheral with compile-time baud rate configuration
pub struct Uart<B: BaudRate> {
    registers: *mut UartRegisters,
    _baud: core::marker::PhantomData<B>,
}

impl<B: BaudRate> Uart<B> {
    /// Create new UART with compile-time baud rate
    pub fn new(base_addr: usize, clock_freq: u32) -> Result<Self, BaudError> {
        let registers = base_addr as *mut UartRegisters;
        
        // Calculate configuration at runtime but with const baud rate
        let config = calculate_baud_config(clock_freq, B::RATE, 16)?;
        
        unsafe {
            (*registers).brr = config.brr_value;
        }
        
        Ok(Uart {
            registers,
            _baud: core::marker::PhantomData,
        })
    }
    
    /// Get the configured baud rate (compile-time constant)
    pub const fn baud_rate(&self) -> u32 {
        B::RATE
    }
}

// Usage:
// let uart = Uart::<Baud115200>::new(0x4001_1000, 72_000_000)?;
```

### Example 3: Advanced Baud Rate Calculator with Tolerance

```rust
use core::fmt;

/// Comprehensive baud rate configuration
#[derive(Debug, Clone, Copy)]
pub struct BaudRateConfig {
    pub mantissa: u32,
    pub fraction: u32,
    pub oversampling: u8,
    pub actual_baud: u32,
    pub error_percent: f32,
    pub tolerance_met: bool,
}

impl fmt::Display for BaudRateConfig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Baud: {} ({}% error), Mantissa: {}, Fraction: {}, Over: {}x",
            self.actual_baud,
            self.error_percent,
            self.mantissa,
            self.fraction,
            self.oversampling
        )
    }
}

/// Baud rate calculator with configurable tolerance
pub struct BaudRateCalculator {
    clock_freq: u32,
    max_error_percent: f32,
}

impl BaudRateCalculator {
    /// Create new calculator with clock frequency and max error tolerance
    pub const fn new(clock_freq: u32, max_error_percent: f32) -> Self {
        Self {
            clock_freq,
            max_error_percent,
        }
    }
    
    /// Calculate baud configuration for given parameters
    fn calculate_internal(
        &self,
        desired_baud: u32,
        oversampling: u8,
    ) -> BaudRateConfig {
        let usartdiv = self.clock_freq as f32 / 
                      (oversampling as f32 * desired_baud as f32);
        
        let mantissa = usartdiv as u32;
        let frac_part = usartdiv - mantissa as f32;
        let mut fraction = (frac_part * 16.0 + 0.5) as u32;
        
        let final_mantissa = if fraction >= 16 {
            fraction = 0;
            mantissa + 1
        } else {
            mantissa
        };
        
        let actual_div = final_mantissa as f32 + (fraction as f32 / 16.0);
        let actual_baud = (self.clock_freq as f32 / 
                          (oversampling as f32 * actual_div)) as u32;
        
        let error_percent = ((actual_baud as f32 - desired_baud as f32) / 
                            desired_baud as f32) * 100.0;
        
        let tolerance_met = error_percent.abs() <= self.max_error_percent;
        
        BaudRateConfig {
            mantissa: final_mantissa,
            fraction,
            oversampling,
            actual_baud,
            error_percent,
            tolerance_met,
        }
    }
    
    /// Find best baud rate configuration
    pub fn calculate(&self, desired_baud: u32) -> Option<BaudRateConfig> {
        // Try 16x oversampling first
        let config_16x = self.calculate_internal(desired_baud, 16);
        if config_16x.tolerance_met {
            return Some(config_16x);
        }
        
        // Try 8x oversampling
        let config_8x = self.calculate_internal(desired_baud, 8);
        if config_8x.tolerance_met {
            return Some(config_8x);
        }
        
        // Return best effort even if tolerance not met
        if config_16x.error_percent.abs() < config_8x.error_percent.abs() {
            Some(config_16x)
        } else {
            Some(config_8x)
        }
    }
    
    /// Calculate BRR register value from configuration
    pub fn to_brr_value(&self, config: &BaudRateConfig) -> u32 {
        (config.mantissa << 4) | (config.fraction & 0x0F)
    }
}

// Usage example
pub fn setup_uart_advanced() -> Result<(), &'static str> {
    const SYSTEM_CLOCK: u32 = 72_000_000; // 72 MHz
    const MAX_ERROR: f32 = 2.0; // 2% tolerance
    
    let calculator = BaudRateCalculator::new(SYSTEM_CLOCK, MAX_ERROR);
    
    let config = calculator.calculate(115200)
        .ok_or("Failed to calculate baud rate")?;
    
    if !config.tolerance_met {
        return Err("Baud rate error exceeds tolerance");
    }
    
    let brr_value = calculator.to_brr_value(&config);
    
    // Apply to hardware
    let uart = 0x4001_1000 as *mut UartRegisters;
    unsafe {
        (*uart).brr = brr_value;
        
        if config.oversampling == 8 {
            (*uart).cr1 |= 1 << 15; // OVER8 bit
        }
    }
    
    Ok(())
}
```

## Summary

**Baud Rate Configuration** is essential for UART communication, determining the data transmission speed between devices. Key points include:

- **Calculation**: Baud rate is derived by dividing the peripheral clock frequency by a programmable divisor and oversampling factor (typically 16x or 8x)
- **Fractional Division**: Modern UARTs support fractional divisors for more precise baud rate generation, reducing error
- **Error Tolerance**: The actual baud rate error should typically be kept below 2-3% for reliable communication; accumulated error increases with frame length
- **Standard Rates**: Common baud rates (9600, 115200, etc.) are chosen for broad compatibility across devices
- **Configuration Trade-offs**: Higher baud rates enable faster data transfer but are more susceptible to noise and require shorter cable lengths

The code examples demonstrate practical implementations across C/C++ and Rust, showing divisor calculation, fractional baud rate support, error checking, and type-safe configuration patterns. Proper baud rate configuration ensures reliable serial communication while maximizing throughput within the constraints of the hardware and transmission medium.