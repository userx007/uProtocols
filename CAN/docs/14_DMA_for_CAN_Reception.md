# DMA for CAN Reception

## Overview

Direct Memory Access (DMA) is a hardware feature that allows peripherals to transfer data directly to/from memory without CPU intervention. For CAN (Controller Area Network) reception, DMA significantly improves efficiency when dealing with high-throughput traffic by offloading the CPU from repetitive data copying tasks.

## Why Use DMA for CAN?

**Traditional Interrupt-Driven Approach:**
- CPU interrupted for each CAN message
- CPU must copy data from CAN peripheral registers to memory
- High CPU overhead at high message rates (>1000 messages/second)
- Potential message loss during high traffic bursts

**DMA Approach:**
- Hardware automatically transfers received CAN data to memory buffers
- CPU only processes data when buffer fills or timer expires
- Minimal CPU overhead regardless of message rate
- Batch processing enables more efficient algorithms

## CAN Bus Basics

CAN is a robust serial communication protocol commonly used in automotive and industrial applications:

- **Multi-master bus**: Any node can transmit when bus is idle
- **Message-based**: Data transmitted in frames with 11-bit or 29-bit identifiers
- **Priority-based arbitration**: Lower ID numbers have higher priority
- **Error detection**: Built-in CRC, acknowledgment, and error handling
- **Typical speeds**: 125 kbps to 1 Mbps (CAN 2.0), up to 5+ Mbps (CAN FD)

**CAN Frame Structure:**
```
| SOF | Identifier | RTR | Control | Data (0-8 bytes) | CRC | ACK | EOF |
```

## Hardware Architecture

```
┌─────────────┐      ┌──────────────┐      ┌─────────────┐
│  CAN Bus    │─────▶│ CAN          │      │   Memory    │
│             │      │ Controller   │      │   Buffer    │
└─────────────┘      │              │      │             │
                     │  ┌────────┐  │      │ ┌─────────┐ │
                     │  │ RX FIFO│  │──┐   │ │Message 0│ │
                     │  └────────┘  │  │   │ ├─────────┤ │
                     │              │  │   │ │Message 1│ │
                     └──────────────┘  │   │ ├─────────┤ │
                            │          │   │ │Message 2│ │
                            ▼          │   │ └─────────┘ │
                     ┌──────────────┐  │   └─────────────┘
                     │ DMA          │  │          ▲
                     │ Controller   │──┘          │
                     │              │─────────────┘
                     └──────────────┘
```

## DMA Configuration Considerations

### 1. Buffer Management Strategies

**Circular Buffer:**
```
Memory: [Msg0][Msg1][Msg2][Msg3][Msg0][Msg1]...
         ▲                      │
         └──────────────────────┘
```
- DMA wraps around when reaching buffer end
- Good for continuous streaming
- Need to track read/write positions

**Ping-Pong Buffers:**
```
Buffer A: [Msg0][Msg1][Msg2][Msg3] ◄── DMA writing
Buffer B: [Msg4][Msg5][Msg6][Msg7] ◄── CPU reading
```
- DMA fills one buffer while CPU processes the other
- Simple synchronization
- Requires double memory

### 2. DMA Triggers

- **Per-message**: DMA triggered for each received message
- **FIFO threshold**: DMA triggered when RX FIFO reaches threshold
- **Timeout**: DMA triggered after period of inactivity

### 3. Memory Alignment

CAN messages must be properly aligned in memory:
```c
// Proper alignment for DMA efficiency
typedef struct __attribute__((aligned(4))) {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
    uint8_t _padding[3];
} can_message_t;
```

## C/C++ Implementation Example

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CAN message structure
typedef struct {
    uint32_t id;           // CAN identifier (11 or 29 bits)
    uint8_t data[8];       // Data payload
    uint8_t dlc;           // Data length code (0-8)
    uint8_t flags;         // Extended ID, RTR, error flags
    uint16_t timestamp;    // Reception timestamp
} can_rx_message_t;

// DMA buffer configuration
#define CAN_DMA_BUFFER_SIZE 128
#define CAN_FIFO_THRESHOLD 8

// Circular buffer for CAN messages
typedef struct {
    can_rx_message_t messages[CAN_DMA_BUFFER_SIZE];
    volatile uint32_t write_index;  // Updated by DMA
    volatile uint32_t read_index;   // Updated by application
    volatile uint32_t overflow_count;
} can_dma_buffer_t;

// Global buffer (placed in DMA-accessible memory region)
__attribute__((section(".dma_buffer")))
static can_dma_buffer_t can_rx_buffer = {0};

// Statistics
typedef struct {
    uint32_t messages_received;
    uint32_t dma_interrupts;
    uint32_t buffer_overflows;
    uint32_t dma_errors;
} can_stats_t;

static volatile can_stats_t can_stats = {0};

/**
 * Initialize CAN peripheral with DMA reception
 * 
 * @param bitrate: CAN bus speed in bps (e.g., 500000 for 500 kbps)
 * @return: true if initialization successful
 */
bool can_dma_init(uint32_t bitrate) {
    // 1. Enable peripheral clocks
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;   // Enable CAN1 clock
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;   // Enable DMA1 clock
    
    // 2. Configure CAN bit timing
    // Example for 500 kbps with 36 MHz APB1 clock
    // Time quantum = 1/(36MHz) * (BRP+1)
    // Bit time = (1 + BS1 + BS2) * time quantum
    CAN1->BTR = (CAN_BTR_BRP_3 |          // Prescaler = 4
                 CAN_BTR_TS1_8 |          // Time segment 1 = 9 TQ
                 CAN_BTR_TS2_1 |          // Time segment 2 = 2 TQ
                 CAN_BTR_SJW_0);          // Resync width = 1 TQ
    
    // 3. Configure CAN operating mode
    CAN1->MCR &= ~CAN_MCR_SLEEP;          // Exit sleep mode
    CAN1->MCR |= CAN_MCR_INRQ;            // Enter initialization mode
    
    // Wait for initialization mode
    while (!(CAN1->MSR & CAN_MSR_INAK));
    
    // 4. Configure filters (accept all messages)
    CAN1->FMR |= CAN_FMR_FINIT;           // Enter filter init mode
    CAN1->FM1R = 0;                       // All filters in mask mode
    CAN1->FS1R = 0x1;                     // Filter 0 in 32-bit mode
    CAN1->FFA1R = 0;                      // Filter 0 to FIFO 0
    CAN1->sFilterRegister[0].FR1 = 0;     // Accept all IDs
    CAN1->sFilterRegister[0].FR2 = 0;
    CAN1->FA1R = 0x1;                     // Activate filter 0
    CAN1->FMR &= ~CAN_FMR_FINIT;          // Exit filter init mode
    
    // 5. Configure DMA for CAN RX FIFO 0
    DMA1_Stream0->CR = 0;                 // Disable stream first
    while (DMA1_Stream0->CR & DMA_SxCR_EN);
    
    DMA1_Stream0->PAR = (uint32_t)&CAN1->sFIFOMailBox[0];  // Source: CAN FIFO
    DMA1_Stream0->M0AR = (uint32_t)can_rx_buffer.messages; // Dest: Buffer
    DMA1_Stream0->NDTR = CAN_DMA_BUFFER_SIZE;              // Number of transfers
    
    DMA1_Stream0->CR = (DMA_SxCR_CHSEL_0 |    // Channel 1 (CAN1_RX0)
                        DMA_SxCR_PL_1 |       // Priority: High
                        DMA_SxCR_MSIZE_1 |    // Memory size: 32-bit
                        DMA_SxCR_PSIZE_1 |    // Peripheral size: 32-bit
                        DMA_SxCR_MINC |       // Memory increment
                        DMA_SxCR_CIRC |       // Circular mode
                        DMA_SxCR_TCIE |       // Transfer complete interrupt
                        DMA_SxCR_HTIE);       // Half transfer interrupt
    
    // 6. Enable DMA requests from CAN
    CAN1->MCR &= ~CAN_MCR_INRQ;           // Exit initialization mode
    while (CAN1->MSR & CAN_MSR_INAK);
    
    // 7. Enable interrupts
    NVIC_SetPriority(DMA1_Stream0_IRQn, 2);
    NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    
    // 8. Start DMA
    DMA1_Stream0->CR |= DMA_SxCR_EN;
    
    return true;
}

/**
 * DMA interrupt handler
 * Called when buffer is half-full or completely full
 */
void DMA1_Stream0_IRQHandler(void) {
    uint32_t flags = DMA1->LISR;
    
    can_stats.dma_interrupts++;
    
    // Half transfer complete
    if (flags & DMA_LISR_HTIF0) {
        DMA1->LIFCR = DMA_LIFCR_CHTIF0;   // Clear flag
        can_rx_buffer.write_index = CAN_DMA_BUFFER_SIZE / 2;
    }
    
    // Transfer complete (buffer full, wrapping)
    if (flags & DMA_LISR_TCIF0) {
        DMA1->LIFCR = DMA_LIFCR_CTCIF0;   // Clear flag
        can_rx_buffer.write_index = 0;
    }
    
    // DMA error
    if (flags & DMA_LISR_TEIF0) {
        DMA1->LIFCR = DMA_LIFCR_CTEIF0;   // Clear flag
        can_stats.dma_errors++;
    }
}

/**
 * Get number of available messages in buffer
 */
uint32_t can_available(void) {
    uint32_t write_idx = can_rx_buffer.write_index;
    uint32_t read_idx = can_rx_buffer.read_index;
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (CAN_DMA_BUFFER_SIZE - read_idx) + write_idx;
    }
}

/**
 * Read a message from the DMA buffer
 * 
 * @param msg: Pointer to store received message
 * @return: true if message available, false if buffer empty
 */
bool can_read_message(can_rx_message_t* msg) {
    if (can_available() == 0) {
        return false;  // No messages available
    }
    
    // Copy message from buffer
    uint32_t read_idx = can_rx_buffer.read_index;
    memcpy(msg, &can_rx_buffer.messages[read_idx], sizeof(can_rx_message_t));
    
    // Update read index (circular)
    can_rx_buffer.read_index = (read_idx + 1) % CAN_DMA_BUFFER_SIZE;
    can_stats.messages_received++;
    
    return true;
}

/**
 * Process all available CAN messages
 * Example: Filter and route messages based on ID
 */
void can_process_messages(void) {
    can_rx_message_t msg;
    
    while (can_read_message(&msg)) {
        // Process based on CAN ID
        if (msg.id == 0x100) {
            // Handle sensor data
            handle_sensor_data(msg.data, msg.dlc);
        } else if ((msg.id & 0x700) == 0x200) {
            // Handle actuator commands (ID range 0x200-0x2FF)
            handle_actuator_command(msg.id & 0xFF, msg.data, msg.dlc);
        } else if (msg.flags & CAN_FLAG_EXTENDED) {
            // Handle extended ID messages
            handle_extended_message(&msg);
        }
    }
}

/**
 * Get CAN statistics
 */
void can_get_stats(can_stats_t* stats) {
    memcpy(stats, (void*)&can_stats, sizeof(can_stats_t));
    stats->buffer_overflows = can_rx_buffer.overflow_count;
}
```

## C++ Implementation with Templates

```cpp
#include <array>
#include <atomic>
#include <optional>
#include <functional>

template<typename MessageType, size_t BufferSize>
class CANDMAReceiver {
public:
    using MessageHandler = std::function<void(const MessageType&)>;
    
private:
    // Circular buffer in DMA-accessible memory
    alignas(32) std::array<MessageType, BufferSize> buffer_;
    std::atomic<uint32_t> write_index_{0};
    std::atomic<uint32_t> read_index_{0};
    std::atomic<uint32_t> overflow_count_{0};
    
    MessageHandler message_handler_;
    
    // Statistics
    struct Stats {
        std::atomic<uint32_t> messages_received{0};
        std::atomic<uint32_t> messages_processed{0};
        std::atomic<uint32_t> dma_interrupts{0};
    } stats_;

public:
    CANDMAReceiver() = default;
    
    /**
     * Initialize DMA for CAN reception
     */
    bool initialize(uint32_t bitrate, void* can_peripheral, void* dma_stream) {
        // Configure CAN peripheral (hardware-specific)
        configure_can_peripheral(can_peripheral, bitrate);
        
        // Configure DMA stream
        configure_dma(dma_stream);
        
        return true;
    }
    
    /**
     * Set callback for message processing
     */
    void set_handler(MessageHandler handler) {
        message_handler_ = std::move(handler);
    }
    
    /**
     * Get number of available messages
     */
    size_t available() const {
        uint32_t write = write_index_.load(std::memory_order_acquire);
        uint32_t read = read_index_.load(std::memory_order_acquire);
        
        if (write >= read) {
            return write - read;
        } else {
            return (BufferSize - read) + write;
        }
    }
    
    /**
     * Read single message (non-blocking)
     */
    std::optional<MessageType> read() {
        if (available() == 0) {
            return std::nullopt;
        }
        
        uint32_t read = read_index_.load(std::memory_order_acquire);
        MessageType msg = buffer_[read];
        
        // Update read index
        uint32_t next = (read + 1) % BufferSize;
        read_index_.store(next, std::memory_order_release);
        
        stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
        
        return msg;
    }
    
    /**
     * Process all available messages
     */
    size_t process_all() {
        size_t count = 0;
        
        while (auto msg = read()) {
            if (message_handler_) {
                message_handler_(*msg);
                count++;
            }
        }
        
        stats_.messages_processed.fetch_add(count, std::memory_order_relaxed);
        return count;
    }
    
    /**
     * Process up to N messages
     */
    size_t process_batch(size_t max_messages) {
        size_t count = 0;
        
        while (count < max_messages) {
            auto msg = read();
            if (!msg) break;
            
            if (message_handler_) {
                message_handler_(*msg);
                count++;
            }
        }
        
        stats_.messages_processed.fetch_add(count, std::memory_order_relaxed);
        return count;
    }
    
    /**
     * DMA interrupt handler (called from ISR)
     */
    void dma_interrupt_handler(bool half_complete, bool full_complete) {
        stats_.dma_interrupts.fetch_add(1, std::memory_order_relaxed);
        
        if (half_complete) {
            write_index_.store(BufferSize / 2, std::memory_order_release);
        }
        
        if (full_complete) {
            write_index_.store(0, std::memory_order_release);
        }
        
        // Check for overflow
        if (available() > BufferSize - 10) {
            overflow_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    /**
     * Get statistics
     */
    auto get_stats() const {
        struct {
            uint32_t messages_received;
            uint32_t messages_processed;
            uint32_t dma_interrupts;
            uint32_t buffer_overflows;
        } result;
        
        result.messages_received = stats_.messages_received.load();
        result.messages_processed = stats_.messages_processed.load();
        result.dma_interrupts = stats_.dma_interrupts.load();
        result.buffer_overflows = overflow_count_.load();
        
        return result;
    }

private:
    void configure_can_peripheral(void* can_base, uint32_t bitrate) {
        // Hardware-specific CAN configuration
        // Similar to C example
    }
    
    void configure_dma(void* dma_stream) {
        // Hardware-specific DMA configuration
        // Configure circular mode, memory increment, etc.
    }
};

// Usage example
struct CANMessage {
    uint32_t id;
    std::array<uint8_t, 8> data;
    uint8_t dlc;
    uint16_t timestamp;
};

CANDMAReceiver<CANMessage, 256> can_receiver;

void setup() {
    can_receiver.initialize(500000, CAN1_BASE, DMA1_Stream0_BASE);
    
    // Set message handler
    can_receiver.set_handler([](const CANMessage& msg) {
        if (msg.id == 0x123) {
            process_sensor_data(msg.data, msg.dlc);
        }
    });
}

void main_loop() {
    while (true) {
        // Process up to 16 messages per loop iteration
        can_receiver.process_batch(16);
        
        // Do other work...
    }
}
```

## Rust Implementation

```rust
use core::sync::atomic::{AtomicU32, AtomicUsize, Ordering};
use core::mem::MaybeUninit;

/// CAN message structure
#[repr(C, align(4))]
#[derive(Debug, Clone, Copy)]
pub struct CanMessage {
    pub id: u32,
    pub data: [u8; 8],
    pub dlc: u8,
    pub flags: u8,
    pub timestamp: u16,
}

impl CanMessage {
    pub fn is_extended(&self) -> bool {
        self.flags & 0x01 != 0
    }
    
    pub fn is_remote(&self) -> bool {
        self.flags & 0x02 != 0
    }
}

/// DMA-based CAN receiver with circular buffer
pub struct CanDmaReceiver<const BUFFER_SIZE: usize> {
    /// Message buffer (must be in DMA-accessible memory)
    buffer: [MaybeUninit<CanMessage>; BUFFER_SIZE],
    
    /// Write index (updated by DMA/ISR)
    write_index: AtomicUsize,
    
    /// Read index (updated by application)
    read_index: AtomicUsize,
    
    /// Statistics
    stats: CanStats,
}

#[derive(Default)]
struct CanStats {
    messages_received: AtomicU32,
    dma_interrupts: AtomicU32,
    buffer_overflows: AtomicU32,
    dma_errors: AtomicU32,
}

impl<const BUFFER_SIZE: usize> CanDmaReceiver<BUFFER_SIZE> {
    /// Create a new CAN DMA receiver
    pub const fn new() -> Self {
        const UNINIT: MaybeUninit<CanMessage> = MaybeUninit::uninit();
        
        Self {
            buffer: [UNINIT; BUFFER_SIZE],
            write_index: AtomicUsize::new(0),
            read_index: AtomicUsize::new(0),
            stats: CanStats {
                messages_received: AtomicU32::new(0),
                dma_interrupts: AtomicU32::new(0),
                buffer_overflows: AtomicU32::new(0),
                dma_errors: AtomicU32::new(0),
            },
        }
    }
    
    /// Initialize CAN peripheral and DMA
    pub fn init(&mut self, bitrate: u32) -> Result<(), Error> {
        // Safety: Hardware register access
        unsafe {
            self.configure_can_peripheral(bitrate)?;
            self.configure_dma()?;
        }
        
        Ok(())
    }
    
    /// Get number of available messages
    pub fn available(&self) -> usize {
        let write = self.write_index.load(Ordering::Acquire);
        let read = self.read_index.load(Ordering::Acquire);
        
        if write >= read {
            write - read
        } else {
            (BUFFER_SIZE - read) + write
        }
    }
    
    /// Read a single message (non-blocking)
    pub fn read_message(&self) -> Option<CanMessage> {
        if self.available() == 0 {
            return None;
        }
        
        let read = self.read_index.load(Ordering::Acquire);
        
        // Safety: We verified message is available
        let msg = unsafe { self.buffer[read].assume_init() };
        
        // Update read index (circular)
        let next = (read + 1) % BUFFER_SIZE;
        self.read_index.store(next, Ordering::Release);
        
        self.stats.messages_received.fetch_add(1, Ordering::Relaxed);
        
        Some(msg)
    }
    
    /// Process all available messages with a closure
    pub fn process_all<F>(&self, mut handler: F) -> usize
    where
        F: FnMut(CanMessage),
    {
        let mut count = 0;
        
        while let Some(msg) = self.read_message() {
            handler(msg);
            count += 1;
        }
        
        count
    }
    
    /// Process up to `max_count` messages
    pub fn process_batch<F>(&self, max_count: usize, mut handler: F) -> usize
    where
        F: FnMut(CanMessage),
    {
        let mut count = 0;
        
        while count < max_count {
            if let Some(msg) = self.read_message() {
                handler(msg);
                count += 1;
            } else {
                break;
            }
        }
        
        count
    }
    
    /// DMA interrupt handler (called from ISR context)
    pub fn dma_interrupt_handler(&self, half_complete: bool, full_complete: bool, error: bool) {
        self.stats.dma_interrupts.fetch_add(1, Ordering::Relaxed);
        
        if half_complete {
            self.write_index.store(BUFFER_SIZE / 2, Ordering::Release);
        }
        
        if full_complete {
            self.write_index.store(0, Ordering::Release);
        }
        
        if error {
            self.stats.dma_errors.fetch_add(1, Ordering::Relaxed);
        }
        
        // Check for potential overflow
        if self.available() > BUFFER_SIZE * 9 / 10 {
            self.stats.buffer_overflows.fetch_add(1, Ordering::Relaxed);
        }
    }
    
    /// Get statistics
    pub fn stats(&self) -> Statistics {
        Statistics {
            messages_received: self.stats.messages_received.load(Ordering::Relaxed),
            dma_interrupts: self.stats.dma_interrupts.load(Ordering::Relaxed),
            buffer_overflows: self.stats.buffer_overflows.load(Ordering::Relaxed),
            dma_errors: self.stats.dma_errors.load(Ordering::Relaxed),
            buffer_usage: self.available(),
        }
    }
    
    unsafe fn configure_can_peripheral(&mut self, bitrate: u32) -> Result<(), Error> {
        // Hardware-specific configuration
        // Example for STM32-like peripheral
        
        let can = &*CAN1::ptr();
        
        // Enter initialization mode
        can.mcr.modify(|_, w| w.inrq().set_bit());
        while can.msr.read().inak().bit_is_clear() {}
        
        // Configure bit timing for specified bitrate
        let timing = calculate_bit_timing(bitrate)?;
        can.btr.write(|w| {
            w.brp().bits(timing.prescaler)
                .ts1().bits(timing.time_seg1)
                .ts2().bits(timing.time_seg2)
                .sjw().bits(timing.sync_jump_width)
        });
        
        // Configure filters (accept all)
        can.fmr.modify(|_, w| w.finit().set_bit());
        can.fa1r.modify(|_, w| w.fact0().set_bit());
        can.fmr.modify(|_, w| w.finit().clear_bit());
        
        // Exit initialization mode
        can.mcr.modify(|_, w| w.inrq().clear_bit());
        while can.msr.read().inak().bit_is_set() {}
        
        Ok(())
    }
    
    unsafe fn configure_dma(&mut self) -> Result<(), Error> {
        let dma = &*DMA1::ptr();
        let stream = &dma.st[0];
        
        // Disable stream
        stream.cr.modify(|_, w| w.en().clear_bit());
        while stream.cr.read().en().bit_is_set() {}
        
        // Configure addresses
        stream.par.write(|w| w.bits(CAN1_RX_FIFO_ADDR));
        stream.m0ar.write(|w| w.bits(self.buffer.as_ptr() as u32));
        stream.ndtr.write(|w| w.bits(BUFFER_SIZE as u32));
        
        // Configure stream
        stream.cr.write(|w| {
            w.chsel().bits(1)  // Channel 1 for CAN1_RX0
                .pl().high()
                .msize().bits32()
                .psize().bits32()
                .minc().set_bit()
                .circ().set_bit()
                .dir().peripheral_to_memory()
                .tcie().set_bit()  // Transfer complete interrupt
                .htie().set_bit()  // Half transfer interrupt
                .teie().set_bit()  // Transfer error interrupt
        });
        
        // Enable stream
        stream.cr.modify(|_, w| w.en().set_bit());
        
        Ok(())
    }
}

// Usage example
static mut CAN_RECEIVER: CanDmaReceiver<256> = CanDmaReceiver::new();

pub fn can_init() -> Result<(), Error> {
    unsafe {
        CAN_RECEIVER.init(500_000)?;
    }
    Ok(())
}

pub fn can_process_messages() {
    unsafe {
        CAN_RECEIVER.process_all(|msg| {
            match msg.id {
                0x100 => handle_sensor_data(&msg),
                0x200..=0x2FF => handle_actuator_command(&msg),
                _ => handle_generic_message(&msg),
            }
        });
    }
}

// DMA interrupt handler
#[interrupt]
fn DMA1_STREAM0() {
    unsafe {
        let dma = &*DMA1::ptr();
        let flags = dma.lisr.read();
        
        let half_complete = flags.htif0().bit_is_set();
        let full_complete = flags.tcif0().bit_is_set();
        let error = flags.teif0().bit_is_set();
        
        // Clear flags
        dma.lifcr.write(|w| {
            w.chtif0().set_bit()
                .ctcif0().set_bit()
                .cteif0().set_bit()
        });
        
        CAN_RECEIVER.dma_interrupt_handler(half_complete, full_complete, error);
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Statistics {
    pub messages_received: u32,
    pub dma_interrupts: u32,
    pub buffer_overflows: u32,
    pub dma_errors: u32,
    pub buffer_usage: usize,
}

#[derive(Debug)]
pub enum Error {
    InvalidBitrate,
    HardwareError,
}

fn handle_sensor_data(msg: &CanMessage) {
    // Process sensor data
}

fn handle_actuator_command(msg: &CanMessage) {
    // Process actuator command
}

fn handle_generic_message(msg: &CanMessage) {
    // Process other messages
}

// Helper function for bit timing calculation
fn calculate_bit_timing(bitrate: u32) -> Result<BitTiming, Error> {
    // Calculate prescaler and time segments based on clock frequency
    // This is simplified - real implementation would be more complex
    Ok(BitTiming {
        prescaler: 4,
        time_seg1: 13,
        time_seg2: 2,
        sync_jump_width: 1,
    })
}

struct BitTiming {
    prescaler: u16,
    time_seg1: u8,
    time_seg2: u8,
    sync_jump_width: u8,
}

// Hardware register definitions (simplified)
const CAN1_RX_FIFO_ADDR: u32 = 0x4000_6400;

struct CAN1;
impl CAN1 {
    fn ptr() -> *const CanRegisters { CAN1_RX_FIFO_ADDR as *const _ }
}

struct DMA1;
impl DMA1 {
    fn ptr() -> *const DmaRegisters { 0x4002_6000 as *const _ }
}

// Placeholder register structures
#[repr(C)]
struct CanRegisters {
    mcr: VolatileCell<u32>,
    msr: VolatileCell<u32>,
    btr: VolatileCell<u32>,
    fmr: VolatileCell<u32>,
    fa1r: VolatileCell<u32>,
}

#[repr(C)]
struct DmaRegisters {
    lisr: VolatileCell<u32>,
    lifcr: VolatileCell<u32>,
    st: [DmaStream; 8],
}

#[repr(C)]
struct DmaStream {
    cr: VolatileCell<u32>,
    ndtr: VolatileCell<u32>,
    par: VolatileCell<u32>,
    m0ar: VolatileCell<u32>,
}

use core::cell::UnsafeCell;
struct VolatileCell<T>(UnsafeCell<T>);
```

## Performance Optimization Tips

### 1. **Cache Management**
```c
// Invalidate cache before reading DMA buffer
SCB_InvalidateDCache_by_Addr((uint32_t*)&can_rx_buffer, sizeof(can_rx_buffer));

// Or disable cache for DMA memory region
MPU_Region_InitTypeDef MPU_InitStruct = {
    .Enable = MPU_REGION_ENABLE,
    .BaseAddress = (uint32_t)&can_rx_buffer,
    .Size = MPU_REGION_SIZE_4KB,
    .AccessPermission = MPU_REGION_FULL_ACCESS,
    .IsBufferable = MPU_ACCESS_NOT_BUFFERABLE,
    .IsCacheable = MPU_ACCESS_NOT_CACHEABLE,
    .IsShareable = MPU_ACCESS_NOT_SHAREABLE,
};
HAL_MPU_ConfigRegion(&MPU_InitStruct);
```

### 2. **Batch Processing**
Process messages in batches rather than one-by-one to reduce interrupt overhead:

```c
// Process messages in batches of 16
void task_can_processing(void) {
    const uint32_t BATCH_SIZE = 16;
    can_rx_message_t batch[BATCH_SIZE];
    
    while (1) {
        uint32_t count = 0;
        
        // Read batch
        while (count < BATCH_SIZE && can_read_message(&batch[count])) {
            count++;
        }
        
        // Process batch
        for (uint32_t i = 0; i < count; i++) {
            process_message(&batch[i]);
        }
        
        if (count == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));  // Sleep if no messages
        }
    }
}
```

### 3. **Minimize Interrupt Frequency**
Use larger buffers and FIFO thresholds to reduce interrupt rate.

## Summary

**DMA for CAN Reception** provides significant performance benefits for high-throughput CAN applications:

**Key Benefits:**
- **Reduced CPU Overhead**: Hardware handles data transfer automatically
- **Higher Throughput**: Can handle 5000+ messages/second with minimal CPU impact
- **Batch Processing**: Enables efficient processing of multiple messages
- **Deterministic Latency**: Predictable response times even under high load

**Implementation Considerations:**
- Use circular buffers for continuous streaming
- Implement proper synchronization between DMA and CPU
- Handle buffer overflow conditions gracefully
- Align data structures for optimal DMA performance
- Consider cache coherency on systems with data cache

**When to Use DMA:**
- Message rates exceeding 500-1000 messages/second
- Systems requiring predictable real-time performance
- Applications with multiple high-speed CAN buses
- CPU-constrained systems needing to maximize efficiency

**Trade-offs:**
- Increased complexity compared to interrupt-driven reception
- Memory overhead for circular buffers
- Requires understanding of hardware DMA controllers
- May require cache management on some architectures

The code examples demonstrate production-ready implementations in C, C++, and Rust, showcasing different approaches to memory safety, concurrency, and hardware abstraction while maintaining high performance.