# UART Interface Implementation for Profibus

## Detailed Description

### Overview
UART (Universal Asynchronous Receiver/Transmitter) interface implementation forms the foundation of the Profibus physical layer, particularly for RS-485 based communications. Profibus DP (Decentralized Periphery) and Profibus PA (Process Automation) variants rely on serial communication interfaces where UART peripherals handle the low-level bit transmission and reception.

### Physical Layer Characteristics

**Profibus-DP (RS-485):**
- Baud rates: 9.6 kbps to 12 Mbps
- Half-duplex communication
- Multi-drop topology (up to 32 nodes without repeaters, 127 with repeaters)
- Manchester or NRZ encoding
- Data format: 11 bits per character (1 start, 8 data, 1 parity, 1 stop)

**Key UART Configuration Requirements:**
- 8 data bits with even parity
- 1 stop bit
- Hardware flow control disabled
- RS-485 transceiver control (transmit enable)
- Precise timing for token passing and frame detection

### Implementation Challenges

1. **Direction Control**: RS-485 requires explicit transmit/receive direction control
2. **Timing Precision**: Profibus has strict timing requirements for token rotation and response times
3. **Frame Detection**: Identifying frame boundaries in continuous data streams
4. **Interrupt Handling**: Efficient processing of high-speed data without loss
5. **Buffer Management**: Handling variable-length frames with minimal latency

### Architecture Components

- **UART Hardware Peripheral**: Physical interface for bit-level communication
- **RS-485 Transceiver**: Converts UART signals to differential RS-485
- **Driver Enable Control**: GPIO pin controlling transmit/receive mode
- **DMA Controller**: Optional for high-speed data transfer
- **Frame Buffer**: Circular or double-buffered storage for incoming/outgoing data
- **Protocol State Machine**: Manages Profibus frame assembly and parsing

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Profibus UART Configuration
#define PROFIBUS_BAUDRATE       1500000  // 1.5 Mbps
#define PROFIBUS_MAX_FRAME_LEN  256
#define PROFIBUS_TIMEOUT_MS     10

// Frame control bytes
#define SD1  0x10  // Start delimiter (frame without data)
#define SD2  0x68  // Start delimiter (variable length frame)
#define SD3  0xA2  // Start delimiter (fixed length frame)
#define SD4  0xDC  // Token frame
#define ED   0x16  // End delimiter
#define SC   0xE5  // Short acknowledgment

// UART registers (platform-specific example)
typedef struct {
    volatile uint32_t DATA;
    volatile uint32_t STATUS;
    volatile uint32_t CTRL;
    volatile uint32_t BAUDRATE;
    volatile uint32_t INT_EN;
    volatile uint32_t INT_STATUS;
} UART_Regs;

// Profibus frame structure
typedef struct {
    uint8_t start_delimiter;
    uint8_t length;
    uint8_t length_repeat;
    uint8_t destination;
    uint8_t source;
    uint8_t function_code;
    uint8_t data[PROFIBUS_MAX_FRAME_LEN];
    uint16_t data_length;
    uint8_t fcs;  // Frame check sequence
    uint8_t end_delimiter;
} ProfibusFrame;

// UART context structure
typedef struct {
    UART_Regs *uart;
    uint32_t baudrate;
    volatile uint8_t *de_gpio;  // Driver enable pin
    uint8_t rx_buffer[PROFIBUS_MAX_FRAME_LEN];
    uint8_t tx_buffer[PROFIBUS_MAX_FRAME_LEN];
    volatile uint16_t rx_index;
    volatile bool frame_received;
    ProfibusFrame current_frame;
} ProfibusUART;

// Initialize UART for Profibus
bool profibus_uart_init(ProfibusUART *ctx, UART_Regs *uart_base, 
                        uint32_t baudrate, volatile uint8_t *de_pin) {
    if (!ctx || !uart_base || !de_pin) return false;
    
    ctx->uart = uart_base;
    ctx->baudrate = baudrate;
    ctx->de_gpio = de_pin;
    ctx->rx_index = 0;
    ctx->frame_received = false;
    
    // Disable UART during configuration
    ctx->uart->CTRL = 0;
    
    // Configure baudrate (platform-specific calculation)
    // Assuming system clock of 48 MHz
    uint32_t divisor = (48000000 / (16 * baudrate)) - 1;
    ctx->uart->BAUDRATE = divisor;
    
    // Configure UART: 8N1 with even parity
    ctx->uart->CTRL = (1 << 0) |   // Enable UART
                      (1 << 1) |   // Enable RX
                      (1 << 2) |   // Enable TX
                      (1 << 4) |   // Even parity
                      (1 << 5) |   // Parity enable
                      (0 << 6);    // 1 stop bit
    
    // Enable RX interrupt
    ctx->uart->INT_EN = (1 << 0);  // RX data available interrupt
    
    // Set DE pin to receive mode
    *ctx->de_gpio = 0;
    
    return true;
}

// Calculate Profibus FCS (Frame Check Sequence)
uint8_t profibus_calculate_fcs(const uint8_t *data, uint16_t length) {
    uint8_t fcs = 0;
    for (uint16_t i = 0; i < length; i++) {
        fcs += data[i];
    }
    return fcs;
}

// Set transceiver direction
static inline void profibus_set_direction(ProfibusUART *ctx, bool transmit) {
    *ctx->de_gpio = transmit ? 1 : 0;
    // Small delay for transceiver switching (typically 1-10 µs)
    for (volatile int i = 0; i < 100; i++);
}

// Transmit a Profibus frame
bool profibus_uart_transmit(ProfibusUART *ctx, const ProfibusFrame *frame) {
    if (!ctx || !frame) return false;
    
    uint16_t tx_index = 0;
    
    // Switch to transmit mode
    profibus_set_direction(ctx, true);
    
    // Build frame based on type
    if (frame->start_delimiter == SD2) {
        // Variable length frame
        ctx->tx_buffer[tx_index++] = SD2;
        ctx->tx_buffer[tx_index++] = frame->length;
        ctx->tx_buffer[tx_index++] = frame->length;  // Repeated
        ctx->tx_buffer[tx_index++] = SD2;            // Repeated start
        ctx->tx_buffer[tx_index++] = frame->destination;
        ctx->tx_buffer[tx_index++] = frame->source;
        ctx->tx_buffer[tx_index++] = frame->function_code;
        
        // Copy data
        for (uint16_t i = 0; i < frame->data_length; i++) {
            ctx->tx_buffer[tx_index++] = frame->data[i];
        }
        
        // Calculate and add FCS
        uint8_t fcs = profibus_calculate_fcs(&ctx->tx_buffer[4], 
                                             frame->data_length + 3);
        ctx->tx_buffer[tx_index++] = fcs;
        ctx->tx_buffer[tx_index++] = ED;
    }
    else if (frame->start_delimiter == SD1) {
        // Fixed length frame (6 bytes)
        ctx->tx_buffer[tx_index++] = SD1;
        ctx->tx_buffer[tx_index++] = frame->destination;
        ctx->tx_buffer[tx_index++] = frame->source;
        ctx->tx_buffer[tx_index++] = frame->function_code;
        uint8_t fcs = profibus_calculate_fcs(&ctx->tx_buffer[1], 3);
        ctx->tx_buffer[tx_index++] = fcs;
        ctx->tx_buffer[tx_index++] = ED;
    }
    
    // Transmit bytes
    for (uint16_t i = 0; i < tx_index; i++) {
        // Wait for TX ready
        while (!(ctx->uart->STATUS & (1 << 1)));
        ctx->uart->DATA = ctx->tx_buffer[i];
    }
    
    // Wait for transmission complete
    while (!(ctx->uart->STATUS & (1 << 2)));
    
    // Switch back to receive mode
    profibus_set_direction(ctx, false);
    
    return true;
}

// UART RX interrupt handler
void profibus_uart_rx_isr(ProfibusUART *ctx) {
    if (!ctx) return;
    
    // Check if data is available
    if (ctx->uart->STATUS & (1 << 0)) {
        uint8_t received_byte = ctx->uart->DATA & 0xFF;
        
        // Store byte in receive buffer
        if (ctx->rx_index < PROFIBUS_MAX_FRAME_LEN) {
            ctx->rx_buffer[ctx->rx_index++] = received_byte;
            
            // Check for end delimiter
            if (received_byte == ED) {
                ctx->frame_received = true;
            }
        } else {
            // Buffer overflow, reset
            ctx->rx_index = 0;
        }
        
        // Clear interrupt flag
        ctx->uart->INT_STATUS = (1 << 0);
    }
}

// Parse received frame
bool profibus_uart_receive(ProfibusUART *ctx, ProfibusFrame *frame) {
    if (!ctx || !frame || !ctx->frame_received) return false;
    
    uint16_t index = 0;
    
    // Parse start delimiter
    frame->start_delimiter = ctx->rx_buffer[index++];
    
    if (frame->start_delimiter == SD2) {
        // Variable length frame
        frame->length = ctx->rx_buffer[index++];
        frame->length_repeat = ctx->rx_buffer[index++];
        
        // Verify length fields match
        if (frame->length != frame->length_repeat) {
            ctx->frame_received = false;
            ctx->rx_index = 0;
            return false;
        }
        
        index++;  // Skip repeated SD2
        frame->destination = ctx->rx_buffer[index++];
        frame->source = ctx->rx_buffer[index++];
        frame->function_code = ctx->rx_buffer[index++];
        
        // Extract data
        frame->data_length = frame->length - 3;
        for (uint16_t i = 0; i < frame->data_length; i++) {
            frame->data[i] = ctx->rx_buffer[index++];
        }
        
        frame->fcs = ctx->rx_buffer[index++];
        frame->end_delimiter = ctx->rx_buffer[index++];
        
        // Verify FCS
        uint8_t calc_fcs = profibus_calculate_fcs(&ctx->rx_buffer[4], 
                                                   frame->data_length + 3);
        if (calc_fcs != frame->fcs) {
            ctx->frame_received = false;
            ctx->rx_index = 0;
            return false;
        }
    }
    
    // Reset for next frame
    ctx->frame_received = false;
    ctx->rx_index = 0;
    
    return true;
}

// Example usage
int main(void) {
    ProfibusUART profibus_ctx;
    UART_Regs *uart0 = (UART_Regs *)0x40000000;  // Example address
    volatile uint8_t *de_pin = (uint8_t *)0x50000010;
    
    // Initialize Profibus UART
    profibus_uart_init(&profibus_ctx, uart0, PROFIBUS_BAUDRATE, de_pin);
    
    // Create and send a frame
    ProfibusFrame tx_frame = {0};
    tx_frame.start_delimiter = SD2;
    tx_frame.length = 6;  // 3 header bytes + 3 data bytes
    tx_frame.destination = 0x02;
    tx_frame.source = 0x01;
    tx_frame.function_code = 0x5C;  // Data exchange
    tx_frame.data[0] = 0xAA;
    tx_frame.data[1] = 0xBB;
    tx_frame.data[2] = 0xCC;
    tx_frame.data_length = 3;
    
    profibus_uart_transmit(&profibus_ctx, &tx_frame);
    
    // Receive frame (in main loop or separate task)
    ProfibusFrame rx_frame;
    while (1) {
        if (profibus_uart_receive(&profibus_ctx, &rx_frame)) {
            // Process received frame
            // ...
        }
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use core::ptr::{read_volatile, write_volatile};
use core::sync::atomic::{AtomicBool, AtomicU16, Ordering};

// Profibus constants
const PROFIBUS_MAX_FRAME_LEN: usize = 256;
const SD1: u8 = 0x10;
const SD2: u8 = 0x68;
const SD3: u8 = 0xA2;
const SD4: u8 = 0xDC;
const ED: u8 = 0x16;
const SC: u8 = 0xE5;

// UART register structure
#[repr(C)]
struct UartRegs {
    data: u32,
    status: u32,
    ctrl: u32,
    baudrate: u32,
    int_en: u32,
    int_status: u32,
}

// Profibus frame structure
#[derive(Debug, Clone)]
pub struct ProfibusFrame {
    pub start_delimiter: u8,
    pub length: u8,
    pub destination: u8,
    pub source: u8,
    pub function_code: u8,
    pub data: [u8; PROFIBUS_MAX_FRAME_LEN],
    pub data_length: u16,
    pub fcs: u8,
    pub end_delimiter: u8,
}

impl Default for ProfibusFrame {
    fn default() -> Self {
        Self {
            start_delimiter: 0,
            length: 0,
            destination: 0,
            source: 0,
            function_code: 0,
            data: [0; PROFIBUS_MAX_FRAME_LEN],
            data_length: 0,
            fcs: 0,
            end_delimiter: 0,
        }
    }
}

// UART context
pub struct ProfibusUart {
    uart_regs: *mut UartRegs,
    de_gpio: *mut u8,
    rx_buffer: [u8; PROFIBUS_MAX_FRAME_LEN],
    tx_buffer: [u8; PROFIBUS_MAX_FRAME_LEN],
    rx_index: AtomicU16,
    frame_received: AtomicBool,
}

impl ProfibusUart {
    /// Create a new Profibus UART instance
    pub fn new(uart_base: usize, de_gpio_addr: usize) -> Self {
        Self {
            uart_regs: uart_base as *mut UartRegs,
            de_gpio: de_gpio_addr as *mut u8,
            rx_buffer: [0; PROFIBUS_MAX_FRAME_LEN],
            tx_buffer: [0; PROFIBUS_MAX_FRAME_LEN],
            rx_index: AtomicU16::new(0),
            frame_received: AtomicBool::new(false),
        }
    }

    /// Initialize UART for Profibus communication
    pub fn init(&mut self, baudrate: u32) -> Result<(), &'static str> {
        unsafe {
            // Disable UART
            write_volatile(&mut (*self.uart_regs).ctrl, 0);

            // Calculate baudrate divisor (assuming 48 MHz system clock)
            let divisor = (48_000_000 / (16 * baudrate)) - 1;
            write_volatile(&mut (*self.uart_regs).baudrate, divisor);

            // Configure UART: 8 bits, even parity, 1 stop bit
            let ctrl_value = (1 << 0) |  // Enable UART
                           (1 << 1) |  // Enable RX
                           (1 << 2) |  // Enable TX
                           (1 << 4) |  // Even parity
                           (1 << 5) |  // Parity enable
                           (0 << 6);   // 1 stop bit
            write_volatile(&mut (*self.uart_regs).ctrl, ctrl_value);

            // Enable RX interrupt
            write_volatile(&mut (*self.uart_regs).int_en, 1 << 0);

            // Set DE pin to receive mode
            write_volatile(self.de_gpio, 0);
        }

        Ok(())
    }

    /// Set transceiver direction
    fn set_direction(&self, transmit: bool) {
        unsafe {
            write_volatile(self.de_gpio, if transmit { 1 } else { 0 });
            // Delay for transceiver switching
            for _ in 0..100 {
                core::hint::spin_loop();
            }
        }
    }

    /// Calculate FCS (Frame Check Sequence)
    fn calculate_fcs(data: &[u8]) -> u8 {
        data.iter().fold(0u8, |acc, &byte| acc.wrapping_add(byte))
    }

    /// Transmit a Profibus frame
    pub fn transmit(&mut self, frame: &ProfibusFrame) -> Result<(), &'static str> {
        let mut tx_index = 0usize;

        // Switch to transmit mode
        self.set_direction(true);

        // Build frame based on type
        match frame.start_delimiter {
            SD2 => {
                // Variable length frame
                self.tx_buffer[tx_index] = SD2;
                tx_index += 1;
                self.tx_buffer[tx_index] = frame.length;
                tx_index += 1;
                self.tx_buffer[tx_index] = frame.length;  // Repeated
                tx_index += 1;
                self.tx_buffer[tx_index] = SD2;  // Repeated start
                tx_index += 1;
                self.tx_buffer[tx_index] = frame.destination;
                tx_index += 1;
                self.tx_buffer[tx_index] = frame.source;
                tx_index += 1;
                self.tx_buffer[tx_index] = frame.function_code;
                tx_index += 1;

                // Copy data
                for i in 0..frame.data_length as usize {
                    self.tx_buffer[tx_index] = frame.data[i];
                    tx_index += 1;
                }

                // Calculate and add FCS
                let fcs = Self::calculate_fcs(&self.tx_buffer[4..tx_index]);
                self.tx_buffer[tx_index] = fcs;
                tx_index += 1;
                self.tx_buffer[tx_index] = ED;
                tx_index += 1;
            }
            SD1 => {
                // Fixed length frame
                self.tx_buffer[0] = SD1;
                self.tx_buffer[1] = frame.destination;
                self.tx_buffer[2] = frame.source;
                self.tx_buffer[3] = frame.function_code;
                let fcs = Self::calculate_fcs(&self.tx_buffer[1..4]);
                self.tx_buffer[4] = fcs;
                self.tx_buffer[5] = ED;
                tx_index = 6;
            }
            _ => return Err("Unsupported frame type"),
        }

        // Transmit bytes
        unsafe {
            for i in 0..tx_index {
                // Wait for TX ready
                while (read_volatile(&(*self.uart_regs).status) & (1 << 1)) == 0 {
                    core::hint::spin_loop();
                }
                write_volatile(&mut (*self.uart_regs).data, self.tx_buffer[i] as u32);
            }

            // Wait for transmission complete
            while (read_volatile(&(*self.uart_regs).status) & (1 << 2)) == 0 {
                core::hint::spin_loop();
            }
        }

        // Switch back to receive mode
        self.set_direction(false);

        Ok(())
    }

    /// RX interrupt handler (call from ISR)
    pub fn rx_isr(&mut self) {
        unsafe {
            // Check if data is available
            if (read_volatile(&(*self.uart_regs).status) & (1 << 0)) != 0 {
                let received_byte = (read_volatile(&(*self.uart_regs).data) & 0xFF) as u8;
                
                let rx_idx = self.rx_index.load(Ordering::Relaxed) as usize;
                
                if rx_idx < PROFIBUS_MAX_FRAME_LEN {
                    self.rx_buffer[rx_idx] = received_byte;
                    self.rx_index.store((rx_idx + 1) as u16, Ordering::Relaxed);
                    
                    // Check for end delimiter
                    if received_byte == ED {
                        self.frame_received.store(true, Ordering::Relaxed);
                    }
                } else {
                    // Buffer overflow, reset
                    self.rx_index.store(0, Ordering::Relaxed);
                }
                
                // Clear interrupt flag
                write_volatile(&mut (*self.uart_regs).int_status, 1 << 0);
            }
        }
    }

    /// Parse received frame
    pub fn receive(&mut self) -> Option<ProfibusFrame> {
        if !self.frame_received.load(Ordering::Relaxed) {
            return None;
        }

        let mut frame = ProfibusFrame::default();
        let mut index = 0usize;

        frame.start_delimiter = self.rx_buffer[index];
        index += 1;

        match frame.start_delimiter {
            SD2 => {
                frame.length = self.rx_buffer[index];
                index += 1;
                let length_repeat = self.rx_buffer[index];
                index += 1;

                // Verify length fields match
                if frame.length != length_repeat {
                    self.frame_received.store(false, Ordering::Relaxed);
                    self.rx_index.store(0, Ordering::Relaxed);
                    return None;
                }

                index += 1;  // Skip repeated SD2
                frame.destination = self.rx_buffer[index];
                index += 1;
                frame.source = self.rx_buffer[index];
                index += 1;
                frame.function_code = self.rx_buffer[index];
                index += 1;

                // Extract data
                frame.data_length = (frame.length - 3) as u16;
                for i in 0..frame.data_length as usize {
                    frame.data[i] = self.rx_buffer[index];
                    index += 1;
                }

                frame.fcs = self.rx_buffer[index];
                index += 1;
                frame.end_delimiter = self.rx_buffer[index];

                // Verify FCS
                let calc_fcs = Self::calculate_fcs(&self.rx_buffer[4..index - 1]);
                if calc_fcs != frame.fcs {
                    self.frame_received.store(false, Ordering::Relaxed);
                    self.rx_index.store(0, Ordering::Relaxed);
                    return None;
                }
            }
            _ => {
                self.frame_received.store(false, Ordering::Relaxed);
                self.rx_index.store(0, Ordering::Relaxed);
                return None;
            }
        }

        // Reset for next frame
        self.frame_received.store(false, Ordering::Relaxed);
        self.rx_index.store(0, Ordering::Relaxed);

        Some(frame)
    }
}

// Example usage
#[no_mangle]
pub extern "C" fn main() -> ! {
    const UART_BASE: usize = 0x40000000;
    const DE_GPIO_ADDR: usize = 0x50000010;
    const BAUDRATE: u32 = 1_500_000;

    let mut profibus = ProfibusUart::new(UART_BASE, DE_GPIO_ADDR);
    profibus.init(BAUDRATE).unwrap();

    // Create and send a frame
    let mut tx_frame = ProfibusFrame::default();
    tx_frame.start_delimiter = SD2;
    tx_frame.length = 6;
    tx_frame.destination = 0x02;
    tx_frame.source = 0x01;
    tx_frame.function_code = 0x5C;
    tx_frame.data[0] = 0xAA;
    tx_frame.data[1] = 0xBB;
    tx_frame.data[2] = 0xCC;
    tx_frame.data_length = 3;

    profibus.transmit(&tx_frame).unwrap();

    // Main loop
    loop {
        if let Some(rx_frame) = profibus.receive() {
            // Process received frame
            // ...
        }
    }
}
```

## Summary

UART interface implementation for Profibus provides the critical physical layer foundation enabling reliable serial communication over RS-485 networks. Key aspects include:

**Core Requirements:**
- Precise 8E1 configuration (8 data bits, even parity, 1 stop bit)
- Baudrate support from 9.6 kbps to 12 Mbps
- RS-485 direction control via driver enable pin
- Frame boundary detection using start/end delimiters
- FCS calculation for data integrity verification

**Implementation Highlights:**
- Hardware UART peripheral configuration with exact timing
- Interrupt-driven reception for real-time responsiveness
- Buffer management for variable-length frames
- Proper RS-485 transceiver switching with settling delays
- Frame parsing state machines for SD1, SD2, SD3, SD4 formats

**Critical Considerations:**
- Direction control latency affects turnaround time
- Interrupt priority must ensure no data loss at high speeds
- DMA can offload CPU for sustained high-throughput applications
- Timing constraints require deterministic interrupt handling
- Error detection through parity, FCS, and length field verification

Both C/C++ and Rust implementations demonstrate bare-metal approaches suitable for embedded systems, with Rust providing additional memory safety guarantees through its ownership model while maintaining zero-cost abstractions for real-time performance.