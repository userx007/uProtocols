# UART Data Frame Structure

## Detailed Description

The UART data frame structure is the fundamental packet format used to transmit information serially between devices. Understanding this structure is critical for implementing reliable serial communication. Each UART frame is a self-contained unit of information that travels bit-by-bit across the transmission line.

### Anatomy of a UART Frame

A complete UART frame consists of several distinct components transmitted sequentially:

**1. Start Bit (1 bit)**
The start bit signals the beginning of a new data frame. The line is normally held HIGH (logic 1) when idle. The start bit is always LOW (logic 0), alerting the receiver that data transmission is about to begin. This transition from idle HIGH to LOW synchronizes the receiver's sampling clock with the incoming data stream.

**2. Data Bits (5-9 bits, typically 8)**
Following the start bit, the actual payload data is transmitted. The most common configuration uses 8 data bits, allowing transmission of a single byte (0-255 or ASCII character). Data can be sent LSB-first (Least Significant Bit first, most common) or MSB-first (Most Significant Bit first), depending on the configuration.

For example, transmitting the ASCII character 'A' (0x41 or 0b01000001) with LSB-first would send: 1-0-0-0-0-0-1-0

**3. Parity Bit (0 or 1 bit, optional)**
The parity bit provides basic error detection. Three modes exist:
- **Even Parity**: Parity bit set so total number of 1s (data + parity) is even
- **Odd Parity**: Parity bit set so total number of 1s is odd
- **None**: No parity bit used (most common in modern systems)

**4. Stop Bits (1, 1.5, or 2 bits)**
Stop bits mark the end of the frame and return the line to its idle HIGH state. They provide time for the receiver to process the received byte before the next start bit arrives. Most configurations use 1 stop bit, but 2 stop bits can provide additional timing margin in noisy environments.

### Complete Frame Example

For transmitting 'A' (0x41) with 8N1 configuration (8 data bits, No parity, 1 stop bit):

```
Idle | Start | D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 | Stop | Idle
 1   |   0   | 1  | 0  | 0  | 0  | 0  | 0  | 1  | 0  |  1   |  1
```

At 9600 baud, each bit takes approximately 104 microseconds.

---

## C/C++ Programming Examples

### Basic UART Frame Transmission (Bare Metal)

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (example for generic microcontroller)
#define UART_DATA_REG    (*((volatile uint32_t*)0x40001000))
#define UART_STATUS_REG  (*((volatile uint32_t*)0x40001004))
#define UART_CONTROL_REG (*((volatile uint32_t*)0x40001008))

// Status register bits
#define UART_TX_READY    (1 << 0)
#define UART_TX_EMPTY    (1 << 1)

// UART configuration structure
typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;      // 5, 6, 7, 8, or 9
    uint8_t stop_bits;      // 1 or 2
    enum {
        PARITY_NONE = 0,
        PARITY_EVEN = 1,
        PARITY_ODD = 2
    } parity;
} uart_config_t;

// Initialize UART with specific frame configuration
void uart_init(const uart_config_t* config) {
    uint32_t control = 0;
    
    // Configure data bits
    control |= ((config->data_bits - 5) & 0x3) << 0;
    
    // Configure stop bits
    if (config->stop_bits == 2) {
        control |= (1 << 2);
    }
    
    // Configure parity
    if (config->parity != PARITY_NONE) {
        control |= (1 << 3);  // Enable parity
        if (config->parity == PARITY_ODD) {
            control |= (1 << 4);
        }
    }
    
    UART_CONTROL_REG = control;
}

// Transmit a single byte (hardware handles frame construction)
void uart_transmit_byte(uint8_t data) {
    // Wait until transmit buffer is ready
    while (!(UART_STATUS_REG & UART_TX_READY));
    
    // Write data to transmit register
    // Hardware automatically adds start bit, parity (if enabled), and stop bits
    UART_DATA_REG = data;
}

// Receive a single byte with frame validation
bool uart_receive_byte(uint8_t* data, bool* parity_error) {
    // Check if data is available
    if (!(UART_STATUS_REG & (1 << 5))) {
        return false;
    }
    
    // Read received data
    uint32_t rx_data = UART_DATA_REG;
    
    // Check for parity error
    *parity_error = (rx_data & (1 << 8)) != 0;
    
    *data = rx_data & 0xFF;
    return true;
}
```

### Software UART Implementation (Bit-Banging)

```c
#include <stdint.h>
#include <stdbool.h>

// GPIO pin definitions
#define TX_PIN 5
#define RX_PIN 6

// Timing for 9600 baud (104 microseconds per bit)
#define BIT_DELAY_US 104

void delay_microseconds(uint32_t us);
void gpio_write(uint8_t pin, bool state);
bool gpio_read(uint8_t pin);

// Calculate parity bit
uint8_t calculate_parity(uint8_t data, bool odd_parity) {
    uint8_t count = 0;
    for (int i = 0; i < 8; i++) {
        if (data & (1 << i)) count++;
    }
    
    if (odd_parity) {
        return (count % 2) == 0 ? 1 : 0;
    } else {
        return (count % 2) == 0 ? 0 : 1;
    }
}

// Software UART transmit with configurable frame
void sw_uart_transmit(uint8_t data, bool use_parity, bool odd_parity, uint8_t stop_bits) {
    // Start bit (LOW)
    gpio_write(TX_PIN, false);
    delay_microseconds(BIT_DELAY_US);
    
    // Data bits (LSB first)
    for (int i = 0; i < 8; i++) {
        gpio_write(TX_PIN, (data >> i) & 0x01);
        delay_microseconds(BIT_DELAY_US);
    }
    
    // Parity bit (optional)
    if (use_parity) {
        uint8_t parity = calculate_parity(data, odd_parity);
        gpio_write(TX_PIN, parity);
        delay_microseconds(BIT_DELAY_US);
    }
    
    // Stop bits (HIGH)
    for (int i = 0; i < stop_bits; i++) {
        gpio_write(TX_PIN, true);
        delay_microseconds(BIT_DELAY_US);
    }
}

// Software UART receive with frame validation
bool sw_uart_receive(uint8_t* data, bool use_parity, bool odd_parity) {
    // Wait for start bit (HIGH to LOW transition)
    while (gpio_read(RX_PIN));
    
    // Delay to center of start bit
    delay_microseconds(BIT_DELAY_US / 2);
    
    // Verify start bit is still LOW
    if (gpio_read(RX_PIN)) {
        return false;  // False start
    }
    
    // Read data bits
    uint8_t received = 0;
    for (int i = 0; i < 8; i++) {
        delay_microseconds(BIT_DELAY_US);
        if (gpio_read(RX_PIN)) {
            received |= (1 << i);
        }
    }
    
    // Read and verify parity bit
    if (use_parity) {
        delay_microseconds(BIT_DELAY_US);
        uint8_t received_parity = gpio_read(RX_PIN) ? 1 : 0;
        uint8_t calculated_parity = calculate_parity(received, odd_parity);
        
        if (received_parity != calculated_parity) {
            return false;  // Parity error
        }
    }
    
    // Wait for stop bit
    delay_microseconds(BIT_DELAY_US);
    if (!gpio_read(RX_PIN)) {
        return false;  // Framing error
    }
    
    *data = received;
    return true;
}
```

---

## Rust Programming Examples

### High-Level UART Frame Configuration

```rust
#[derive(Debug, Clone, Copy)]
pub enum Parity {
    None,
    Even,
    Odd,
}

#[derive(Debug, Clone, Copy)]
pub enum StopBits {
    One,
    Two,
}

#[derive(Debug, Clone, Copy)]
pub enum DataBits {
    Five = 5,
    Six = 6,
    Seven = 7,
    Eight = 8,
    Nine = 9,
}

#[derive(Debug, Clone, Copy)]
pub struct UartConfig {
    pub baud_rate: u32,
    pub data_bits: DataBits,
    pub parity: Parity,
    pub stop_bits: StopBits,
}

impl Default for UartConfig {
    fn default() -> Self {
        // Standard 8N1 configuration
        UartConfig {
            baud_rate: 9600,
            data_bits: DataBits::Eight,
            parity: Parity::None,
            stop_bits: StopBits::One,
        }
    }
}

// Frame validation errors
#[derive(Debug, PartialEq)]
pub enum FrameError {
    ParityError,
    FramingError,
    OverrunError,
}

pub struct UartFrame {
    pub data: u8,
    pub config: UartConfig,
}

impl UartFrame {
    pub fn new(data: u8, config: UartConfig) -> Self {
        UartFrame { data, config }
    }
    
    // Calculate total frame length in bits
    pub fn frame_length(&self) -> usize {
        let mut length = 1;  // Start bit
        length += self.config.data_bits as usize;
        length += match self.config.parity {
            Parity::None => 0,
            _ => 1,
        };
        length += match self.config.stop_bits {
            StopBits::One => 1,
            StopBits::Two => 2,
        };
        length
    }
    
    // Calculate parity bit value
    pub fn calculate_parity(&self) -> Option<bool> {
        match self.config.parity {
            Parity::None => None,
            Parity::Even => {
                let ones = self.data.count_ones();
                Some(ones % 2 == 1)
            }
            Parity::Odd => {
                let ones = self.data.count_ones();
                Some(ones % 2 == 0)
            }
        }
    }
    
    // Get frame as bit sequence (for visualization or bit-banging)
    pub fn to_bits(&self) -> Vec<bool> {
        let mut bits = Vec::new();
        
        // Start bit (always 0/false)
        bits.push(false);
        
        // Data bits (LSB first)
        for i in 0..8 {
            bits.push((self.data >> i) & 0x01 == 1);
        }
        
        // Parity bit
        if let Some(parity) = self.calculate_parity() {
            bits.push(parity);
        }
        
        // Stop bits (always 1/true)
        let stop_count = match self.config.stop_bits {
            StopBits::One => 1,
            StopBits::Two => 2,
        };
        for _ in 0..stop_count {
            bits.push(true);
        }
        
        bits
    }
}
```

### Embedded HAL UART Implementation

```rust
use embedded_hal::serial::{Read, Write};

pub struct Uart<T> {
    peripheral: T,
    config: UartConfig,
}

impl<T> Uart<T> {
    pub fn new(peripheral: T, config: UartConfig) -> Self {
        Uart { peripheral, config }
    }
    
    pub fn transmit_frame(&mut self, data: u8) -> Result<(), FrameError> 
    where
        T: Write<u8>,
    {
        // Hardware handles frame construction
        self.peripheral.write(data)
            .map_err(|_| FrameError::OverrunError)
    }
    
    pub fn receive_frame(&mut self) -> Result<u8, FrameError>
    where
        T: Read<u8>,
    {
        match self.peripheral.read() {
            Ok(data) => Ok(data),
            Err(_) => Err(FrameError::FramingError),
        }
    }
}
```

### Software UART Implementation in Rust

```rust
use core::time::Duration;

pub struct SoftwareUart {
    tx_pin: u8,
    rx_pin: u8,
    config: UartConfig,
    bit_duration: Duration,
}

impl SoftwareUart {
    pub fn new(tx_pin: u8, rx_pin: u8, config: UartConfig) -> Self {
        let bit_duration = Duration::from_micros(1_000_000 / config.baud_rate as u64);
        
        SoftwareUart {
            tx_pin,
            rx_pin,
            config,
            bit_duration,
        }
    }
    
    // Calculate parity for given data
    fn calculate_parity(&self, data: u8) -> Option<bool> {
        match self.config.parity {
            Parity::None => None,
            Parity::Even => Some(data.count_ones() % 2 == 1),
            Parity::Odd => Some(data.count_ones() % 2 == 0),
        }
    }
    
    // Transmit a byte with proper frame structure
    pub fn transmit(&mut self, data: u8) {
        // Start bit (LOW)
        self.set_tx_pin(false);
        self.delay(self.bit_duration);
        
        // Data bits (LSB first)
        for i in 0..8 {
            let bit = (data >> i) & 0x01 == 1;
            self.set_tx_pin(bit);
            self.delay(self.bit_duration);
        }
        
        // Parity bit
        if let Some(parity) = self.calculate_parity(data) {
            self.set_tx_pin(parity);
            self.delay(self.bit_duration);
        }
        
        // Stop bits (HIGH)
        let stop_count = match self.config.stop_bits {
            StopBits::One => 1,
            StopBits::Two => 2,
        };
        
        for _ in 0..stop_count {
            self.set_tx_pin(true);
            self.delay(self.bit_duration);
        }
    }
    
    // Receive a byte with frame validation
    pub fn receive(&mut self) -> Result<u8, FrameError> {
        // Wait for start bit (HIGH to LOW transition)
        while self.read_rx_pin() {}
        
        // Delay to center of start bit
        self.delay(self.bit_duration / 2);
        
        // Verify start bit is still LOW
        if self.read_rx_pin() {
            return Err(FrameError::FramingError);
        }
        
        // Read data bits
        let mut data = 0u8;
        for i in 0..8 {
            self.delay(self.bit_duration);
            if self.read_rx_pin() {
                data |= 1 << i;
            }
        }
        
        // Verify parity if enabled
        if let Some(expected_parity) = self.calculate_parity(data) {
            self.delay(self.bit_duration);
            let received_parity = self.read_rx_pin();
            
            if received_parity != expected_parity {
                return Err(FrameError::ParityError);
            }
        }
        
        // Verify stop bit
        self.delay(self.bit_duration);
        if !self.read_rx_pin() {
            return Err(FrameError::FramingError);
        }
        
        Ok(data)
    }
    
    // Hardware abstraction methods (to be implemented for specific platform)
    fn set_tx_pin(&mut self, state: bool) {
        // Platform-specific GPIO write
    }
    
    fn read_rx_pin(&self) -> bool {
        // Platform-specific GPIO read
        true
    }
    
    fn delay(&self, duration: Duration) {
        // Platform-specific delay
    }
}
```

---

## Summary

The UART data frame structure is a carefully designed packet format that enables reliable asynchronous serial communication. Each frame contains a start bit for synchronization, 5-9 data bits for payload transmission (typically 8), an optional parity bit for error detection, and 1-2 stop bits to mark frame completion. The most common configuration is "8N1" (8 data bits, no parity, 1 stop bit), which provides a good balance between efficiency and reliability.

Understanding frame structure is essential because both transmitter and receiver must agree on the exact configuration - mismatched settings result in garbled data. The start bit's HIGH-to-LOW transition synchronizes the receiver's sampling clock, allowing it to accurately capture each subsequent bit at the center of its timing window. Parity bits provide basic error detection but cannot correct errors, while stop bits ensure adequate inter-frame spacing.

In practice, hardware UART peripherals automatically construct and deconstruct frames, but understanding the underlying structure is crucial for debugging communication issues, implementing software UART solutions, analyzing protocol timing with oscilloscopes or logic analyzers, and optimizing throughput by selecting appropriate frame configurations for specific applications.