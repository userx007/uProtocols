# Interrupt-Driven CAN Handling

## Overview

Interrupt-driven CAN handling is a crucial technique for achieving low-latency, real-time message processing in Controller Area Network (CAN) applications. Instead of continuously polling the CAN controller for new messages or status changes, the system responds immediately to hardware interrupts, allowing the CPU to perform other tasks while waiting for CAN events.

## Why Interrupt-Driven Handling?

**Advantages:**
- **Low Latency**: Messages are processed immediately upon reception
- **CPU Efficiency**: No wasted cycles polling hardware registers
- **Real-time Response**: Critical for time-sensitive automotive and industrial applications
- **Power Efficiency**: CPU can enter low-power states between events
- **Deterministic Timing**: Predictable response times for safety-critical systems

**Common CAN Interrupt Sources:**
1. **RX Interrupt**: New message received in mailbox/FIFO
2. **TX Interrupt**: Message transmission completed
3. **Error Interrupt**: Bus error, arbitration loss, or error state change
4. **Wake-up Interrupt**: Bus activity detected in sleep mode
5. **FIFO Overflow**: Receive buffer overflow condition

## Core Concepts

### Interrupt Service Routine (ISR) Design

An ISR should be:
- **Fast**: Minimal processing time to avoid blocking other interrupts
- **Non-blocking**: No delays or lengthy operations
- **Atomic**: Protected against race conditions with shared data
- **Predictable**: Deterministic execution time

### Typical ISR Flow

```
Interrupt Triggered → Save Context → Identify Source → 
Quick Processing → Signal Task/Thread → Restore Context → Return
```

## C/C++ Implementation Examples

### Example 1: Basic CAN ISR Structure (Bare Metal)

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (example for STM32-like MCU)
#define CAN1_BASE 0x40006400
#define CAN_TSR   (*(volatile uint32_t*)(CAN1_BASE + 0x08))
#define CAN_RF0R  (*(volatile uint32_t*)(CAN1_BASE + 0x0C))
#define CAN_IER   (*(volatile uint32_t*)(CAN1_BASE + 0x14))
#define CAN_ESR   (*(volatile uint32_t*)(CAN1_BASE + 0x18))

// Interrupt enable bits
#define CAN_IER_TMEIE  (1 << 0)  // Transmit mailbox empty
#define CAN_IER_FMPIE0 (1 << 1)  // FIFO 0 message pending
#define CAN_IER_ERRIE  (1 << 15) // Error interrupt

// Status bits
#define CAN_RF0R_FMP0  (0x03)    // FIFO 0 message pending count
#define CAN_RF0R_RFOM0 (1 << 5)  // Release FIFO 0 output mailbox

// Ring buffer for received messages
#define RX_BUFFER_SIZE 32

typedef struct {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
    uint32_t timestamp;
} can_message_t;

typedef struct {
    can_message_t buffer[RX_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
} can_rx_buffer_t;

static can_rx_buffer_t rx_buffer = {0};
static volatile uint32_t tx_complete_count = 0;
static volatile uint32_t error_count = 0;

// Read message from hardware FIFO
static inline void can_read_fifo(can_message_t *msg) {
    volatile uint32_t *fifo_base = (volatile uint32_t*)(CAN1_BASE + 0x1B0);
    
    uint32_t rir = fifo_base[0];
    uint32_t rdtr = fifo_base[1];
    uint32_t rdlr = fifo_base[2];
    uint32_t rdhr = fifo_base[3];
    
    // Extract CAN ID (standard or extended)
    if (rir & (1 << 2)) { // IDE bit - extended ID
        msg->id = ((rir >> 3) & 0x1FFFFFFF) | 0x80000000;
    } else {
        msg->id = (rir >> 21) & 0x7FF;
    }
    
    msg->dlc = rdtr & 0x0F;
    msg->timestamp = (rdtr >> 16) & 0xFFFF;
    
    // Copy data bytes
    msg->data[0] = (rdlr >> 0) & 0xFF;
    msg->data[1] = (rdlr >> 8) & 0xFF;
    msg->data[2] = (rdlr >> 16) & 0xFF;
    msg->data[3] = (rdlr >> 24) & 0xFF;
    msg->data[4] = (rdhr >> 0) & 0xFF;
    msg->data[5] = (rdhr >> 8) & 0xFF;
    msg->data[6] = (rdhr >> 16) & 0xFF;
    msg->data[7] = (rdhr >> 24) & 0xFF;
}

// CAN RX Interrupt Handler
void CAN1_RX0_IRQHandler(void) {
    // Check if FIFO 0 has messages
    if (CAN_RF0R & CAN_RF0R_FMP0) {
        uint32_t next_head = (rx_buffer.head + 1) % RX_BUFFER_SIZE;
        
        // Check for buffer overflow
        if (next_head == rx_buffer.tail) {
            rx_buffer.overflow_count++;
            // Still read to clear the hardware FIFO
            can_message_t dummy;
            can_read_fifo(&dummy);
        } else {
            // Read message into ring buffer
            can_read_fifo(&rx_buffer.buffer[rx_buffer.head]);
            rx_buffer.head = next_head;
        }
        
        // Release FIFO output mailbox
        CAN_RF0R |= CAN_RF0R_RFOM0;
    }
}

// CAN TX Interrupt Handler
void CAN1_TX_IRQHandler(void) {
    // Check transmit mailbox empty flags
    if (CAN_TSR & 0x07) { // Any mailbox empty
        tx_complete_count++;
        // Could signal a semaphore or set a flag here
    }
}

// CAN Error Interrupt Handler
void CAN1_SCE_IRQHandler(void) {
    uint32_t esr = CAN_ESR;
    
    // Check for specific errors
    if (esr & (1 << 0)) { // Error warning flag
        error_count++;
    }
    
    if (esr & (1 << 1)) { // Error passive flag
        error_count++;
    }
    
    if (esr & (1 << 2)) { // Bus-off flag
        error_count++;
        // Critical error - might need to reset CAN peripheral
    }
    
    // Clear error flags
    CAN_ESR = esr;
}

// Application-level message retrieval (non-ISR context)
bool can_get_message(can_message_t *msg) {
    if (rx_buffer.head == rx_buffer.tail) {
        return false; // Buffer empty
    }
    
    // Critical section - disable interrupts briefly
    __disable_irq();
    *msg = rx_buffer.buffer[rx_buffer.tail];
    rx_buffer.tail = (rx_buffer.tail + 1) % RX_BUFFER_SIZE;
    __enable_irq();
    
    return true;
}

// Initialization
void can_init_interrupts(void) {
    // Enable CAN interrupts
    CAN_IER |= CAN_IER_FMPIE0 | CAN_IER_TMEIE | CAN_IER_ERRIE;
    
    // Enable NVIC interrupts (example priorities)
    NVIC_SetPriority(CAN1_RX0_IRQn, 1);  // High priority for RX
    NVIC_SetPriority(CAN1_TX_IRQn, 2);   // Medium priority for TX
    NVIC_SetPriority(CAN1_SCE_IRQn, 0);  // Highest for errors
    
    NVIC_EnableIRQ(CAN1_RX0_IRQn);
    NVIC_EnableIRQ(CAN1_TX_IRQn);
    NVIC_EnableIRQ(CAN1_SCE_IRQn);
}
```

### Example 2: RTOS-Based CAN Handler with FreeRTOS

```cpp
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

class CANInterruptHandler {
private:
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
    SemaphoreHandle_t tx_semaphore;
    
    struct CANMessage {
        uint32_t id;
        uint8_t data[8];
        uint8_t dlc;
        bool is_extended;
        TickType_t timestamp;
    };
    
    struct ErrorInfo {
        uint32_t rx_error_count;
        uint32_t tx_error_count;
        uint32_t bus_off_count;
        uint32_t last_error_code;
    };
    
    ErrorInfo error_info;
    
public:
    CANInterruptHandler() : error_info{0} {
        // Create queues with sufficient depth
        rx_queue = xQueueCreate(64, sizeof(CANMessage));
        tx_queue = xQueueCreate(16, sizeof(CANMessage));
        tx_semaphore = xSemaphoreCreateBinary();
    }
    
    // Called from ISR context
    void handleRxInterrupt() {
        BaseType_t higher_priority_woken = pdFALSE;
        CANMessage msg;
        
        // Read from hardware (pseudo-code)
        if (readHardwareMessage(&msg)) {
            msg.timestamp = xTaskGetTickCountFromISR();
            
            // Send to queue (with timeout of 0 - don't block in ISR)
            if (xQueueSendFromISR(rx_queue, &msg, &higher_priority_woken) != pdTRUE) {
                // Queue full - message lost
                error_info.rx_error_count++;
            }
        }
        
        // Yield to higher priority task if necessary
        portYIELD_FROM_ISR(higher_priority_woken);
    }
    
    // Called from ISR context
    void handleTxInterrupt() {
        BaseType_t higher_priority_woken = pdFALSE;
        CANMessage msg;
        
        // Check if there are pending messages to transmit
        if (xQueueReceiveFromISR(tx_queue, &msg, &higher_priority_woken) == pdTRUE) {
            // Write to hardware mailbox
            writeHardwareMessage(&msg);
        }
        
        // Signal transmission complete
        xSemaphoreGiveFromISR(tx_semaphore, &higher_priority_woken);
        portYIELD_FROM_ISR(higher_priority_woken);
    }
    
    // Called from ISR context
    void handleErrorInterrupt(uint32_t error_flags) {
        if (error_flags & CAN_ERROR_BUS_OFF) {
            error_info.bus_off_count++;
            // Trigger recovery mechanism
        }
        
        if (error_flags & CAN_ERROR_WARNING) {
            error_info.last_error_code = error_flags;
        }
        
        error_info.tx_error_count = getHardwareTxErrorCount();
        error_info.rx_error_count = getHardwareRxErrorCount();
    }
    
    // Application task interface (non-ISR)
    bool receiveMessage(CANMessage &msg, TickType_t timeout) {
        return xQueueReceive(rx_queue, &msg, timeout) == pdTRUE;
    }
    
    bool transmitMessage(const CANMessage &msg, TickType_t timeout) {
        if (xQueueSend(tx_queue, &msg, timeout) == pdTRUE) {
            // Trigger TX interrupt if mailbox available
            triggerHardwareTx();
            
            // Wait for transmission complete
            return xSemaphoreTake(tx_semaphore, timeout) == pdTRUE;
        }
        return false;
    }
    
    ErrorInfo getErrorInfo() const {
        return error_info;
    }
    
private:
    bool readHardwareMessage(CANMessage *msg);
    void writeHardwareMessage(const CANMessage *msg);
    void triggerHardwareTx();
    uint32_t getHardwareTxErrorCount();
    uint32_t getHardwareRxErrorCount();
};

// Global instance
static CANInterruptHandler can_handler;

// ISR wrappers (called by hardware)
extern "C" {
    void CAN1_RX0_IRQHandler(void) {
        can_handler.handleRxInterrupt();
    }
    
    void CAN1_TX_IRQHandler(void) {
        can_handler.handleTxInterrupt();
    }
    
    void CAN1_SCE_IRQHandler(void) {
        uint32_t error_flags = CAN_ESR; // Read error register
        can_handler.handleErrorInterrupt(error_flags);
    }
}

// Example application task
void canProcessingTask(void *params) {
    CANInterruptHandler::CANMessage msg;
    
    while (1) {
        if (can_handler.receiveMessage(msg, portMAX_DELAY)) {
            // Process message based on ID
            switch (msg.id) {
                case 0x100:
                    processSpeedMessage(msg.data, msg.dlc);
                    break;
                case 0x200:
                    processTemperatureMessage(msg.data, msg.dlc);
                    break;
                default:
                    // Unknown message
                    break;
            }
        }
    }
}
```

## Rust Implementation Examples

### Example 1: Embedded Rust with RTIC (Real-Time Interrupt-driven Concurrency)

```rust
#![no_std]
#![no_main]

use panic_halt as _;
use rtic::app;
use heapless::{spsc::Queue, Vec};

#[derive(Clone, Copy, Debug)]
pub struct CanMessage {
    pub id: u32,
    pub data: [u8; 8],
    pub dlc: u8,
    pub timestamp: u32,
}

#[derive(Clone, Copy, Debug)]
pub enum CanError {
    BusOff,
    ErrorWarning,
    ErrorPassive,
    ArbitrationLost,
    FifoOverrun,
}

const RX_QUEUE_SIZE: usize = 32;
const TX_QUEUE_SIZE: usize = 16;

#[app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [EXTI0, EXTI1])]
mod app {
    use super::*;
    use stm32f4xx_hal::{
        can::{Can, Instance},
        pac::CAN1,
        prelude::*,
    };
    
    #[shared]
    struct Shared {
        can: Can<CAN1>,
        error_count: u32,
    }
    
    #[local]
    struct Local {
        rx_queue: Queue<CanMessage, RX_QUEUE_SIZE>,
        tx_queue: Queue<CanMessage, TX_QUEUE_SIZE>,
    }
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local, init::Monotonics) {
        let dp = ctx.device;
        
        // Initialize CAN peripheral (simplified)
        let can = {
            let gpioa = dp.GPIOA.split();
            let rx = gpioa.pa11.into_alternate();
            let tx = gpioa.pa12.into_alternate();
            
            let mut can = Can::new(dp.CAN1, (tx, rx));
            
            // Configure filters, timing, etc.
            can.configure(|config| {
                config.set_bit_timing(0x001c0003); // 500 kbps
                config.set_automatic_retransmit(true);
            });
            
            // Enable interrupts
            can.enable_interrupts(
                Interrupt::Fifo0MessagePending |
                Interrupt::TransmitMailboxEmpty |
                Interrupt::Error
            );
            
            can
        };
        
        (
            Shared {
                can,
                error_count: 0,
            },
            Local {
                rx_queue: Queue::new(),
                tx_queue: Queue::new(),
            },
            init::Monotonics(),
        )
    }
    
    // CAN RX Interrupt - highest priority
    #[task(binds = CAN1_RX0, shared = [can], local = [rx_queue], priority = 3)]
    fn can_rx0(mut ctx: can_rx0::Context) {
        ctx.shared.can.lock(|can| {
            // Read all pending messages from FIFO
            while let Some(frame) = can.receive() {
                let msg = CanMessage {
                    id: frame.id().as_raw(),
                    data: frame.data().try_into().unwrap_or([0; 8]),
                    dlc: frame.dlc() as u8,
                    timestamp: monotonics::now().ticks(),
                };
                
                // Enqueue message (lock-free)
                if ctx.local.rx_queue.enqueue(msg).is_err() {
                    // Queue full - message dropped
                    // Could trigger an error counter increment
                }
            }
        });
        
        // Spawn processing task
        process_rx_messages::spawn().ok();
    }
    
    // CAN TX Interrupt
    #[task(binds = CAN1_TX, shared = [can], local = [tx_queue], priority = 2)]
    fn can_tx(mut ctx: can_tx::Context) {
        ctx.shared.can.lock(|can| {
            // Clear interrupt flags
            can.clear_interrupt_flags(Interrupt::TransmitMailboxEmpty);
            
            // Try to send pending messages
            while let Some(msg) = ctx.local.tx_queue.dequeue() {
                let frame = CanFrame::new(
                    CanId::new(msg.id),
                    &msg.data[..msg.dlc as usize],
                );
                
                if can.transmit(&frame).is_err() {
                    // Mailbox full, re-queue and try later
                    ctx.local.tx_queue.enqueue(msg).ok();
                    break;
                }
            }
        });
    }
    
    // CAN Error Interrupt - highest priority
    #[task(binds = CAN1_SCE, shared = [can, error_count], priority = 4)]
    fn can_error(mut ctx: can_error::Context) {
        ctx.shared.can.lock(|can| {
            let error_status = can.error_status();
            
            ctx.shared.error_count.lock(|count| {
                *count += 1;
            });
            
            // Handle specific errors
            if error_status.is_bus_off() {
                // Critical: bus-off state
                // Might need to reset and re-initialize CAN
                handle_bus_off_error();
            }
            
            if error_status.is_error_passive() {
                // Error passive state
                handle_error_passive();
            }
            
            // Clear error interrupt flags
            can.clear_interrupt_flags(Interrupt::Error);
        });
    }
    
    // Lower priority task to process received messages
    #[task(local = [rx_queue], priority = 1)]
    fn process_rx_messages(ctx: process_rx_messages::Context) {
        while let Some(msg) = ctx.local.rx_queue.dequeue() {
            // Process message based on ID
            match msg.id {
                0x100 => process_speed_message(&msg),
                0x200 => process_temperature_message(&msg),
                0x300..=0x3FF => process_sensor_range(&msg),
                _ => {} // Unknown or unhandled message
            }
        }
    }
    
    // Public API to send message from application
    #[task(shared = [can], local = [tx_queue], priority = 1)]
    fn send_can_message(ctx: send_can_message::Context, msg: CanMessage) {
        // Try immediate transmission
        let frame = CanFrame::new(
            CanId::new(msg.id),
            &msg.data[..msg.dlc as usize],
        );
        
        ctx.shared.can.lock(|can| {
            if can.transmit(&frame).is_err() {
                // Mailbox full, queue for later
                ctx.local.tx_queue.enqueue(msg).ok();
            }
        });
    }
}

fn process_speed_message(msg: &CanMessage) {
    // Extract speed from message data
    let speed = u16::from_le_bytes([msg.data[0], msg.data[1]]);
    // Handle speed update
}

fn process_temperature_message(msg: &CanMessage) {
    // Extract temperature
    let temp = i16::from_le_bytes([msg.data[0], msg.data[1]]);
    // Handle temperature update
}

fn process_sensor_range(msg: &CanMessage) {
    // Handle sensor messages in range 0x300-0x3FF
}

fn handle_bus_off_error() {
    // Implement bus-off recovery
}

fn handle_error_passive() {
    // Log or handle error passive state
}
```

### Example 2: Async Rust with Embassy Framework

```rust
#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts,
    can::{Can, RxMode, Envelope, Frame},
    peripherals,
};
use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex,
    channel::Channel,
    signal::Signal,
};
use embassy_time::{Duration, Instant, Timer};

// Static channels for inter-task communication
static RX_CHANNEL: Channel<CriticalSectionRawMutex, CanMessage, 32> = Channel::new();
static TX_CHANNEL: Channel<CriticalSectionRawMutex, CanMessage, 16> = Channel::new();
static ERROR_SIGNAL: Signal<CriticalSectionRawMutex, CanError> = Signal::new();

#[derive(Clone, Copy, Debug)]
pub struct CanMessage {
    pub id: u32,
    pub data: [u8; 8],
    pub dlc: u8,
    pub timestamp: Instant,
}

#[derive(Clone, Copy, Debug)]
pub enum CanError {
    BusOff,
    ErrorWarning,
    Overrun,
}

bind_interrupts!(struct Irqs {
    CAN1_RX0 => embassy_stm32::can::Rx0InterruptHandler<peripherals::CAN1>;
    CAN1_TX => embassy_stm32::can::TxInterruptHandler<peripherals::CAN1>;
    CAN1_SCE => embassy_stm32::can::SceInterruptHandler<peripherals::CAN1>;
});

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());
    
    // Initialize CAN peripheral
    let mut can = Can::new(
        p.CAN1,
        p.PA11, // RX
        p.PA12, // TX
        Irqs,
    );
    
    // Configure CAN timing for 500 kbps
    can.modify_config()
        .set_bit_timing(0x001c0003)
        .set_automatic_retransmit(true)
        .enable();
    
    // Spawn concurrent tasks
    spawner.spawn(can_rx_task(can)).unwrap();
    spawner.spawn(can_tx_task()).unwrap();
    spawner.spawn(can_error_handler()).unwrap();
    spawner.spawn(application_task()).unwrap();
}

// High-priority RX task (interrupt-driven via Embassy)
#[embassy_executor::task]
async fn can_rx_task(mut can: Can<'static, peripherals::CAN1>) {
    loop {
        // This await is interrupt-driven - no polling
        match can.read().await {
            Ok(envelope) => {
                let frame = envelope.frame();
                let msg = CanMessage {
                    id: frame.id().as_raw(),
                    data: {
                        let mut data = [0u8; 8];
                        let len = frame.data().len().min(8);
                        data[..len].copy_from_slice(&frame.data()[..len]);
                        data
                    },
                    dlc: frame.data().len() as u8,
                    timestamp: Instant::now(),
                };
                
                // Send to processing channel (async, won't block)
                RX_CHANNEL.send(msg).await;
            }
            Err(e) => {
                // Handle RX error
                ERROR_SIGNAL.signal(CanError::Overrun);
            }
        }
    }
}

// TX task that sends queued messages
#[embassy_executor::task]
async fn can_tx_task() {
    loop {
        // Wait for message to transmit
        let msg = TX_CHANNEL.receive().await;
        
        // Create frame
        let frame = Frame::new_data(
            embedded_can::Id::Standard(
                embedded_can::StandardId::new(msg.id as u16).unwrap()
            ),
            &msg.data[..msg.dlc as usize],
        );
        
        // This transmit is also interrupt-driven
        // It will await until mailbox is available
        if let Err(e) = can.write(&frame).await {
            // Handle TX error
            ERROR_SIGNAL.signal(CanError::BusOff);
        }
    }
}

// Error monitoring task
#[embassy_executor::task]
async fn can_error_handler() {
    loop {
        let error = ERROR_SIGNAL.wait().await;
        
        match error {
            CanError::BusOff => {
                // Critical error - might need recovery
                defmt::error!("CAN bus-off error detected");
                // Implement recovery procedure
            }
            CanError::ErrorWarning => {
                defmt::warn!("CAN error warning");
            }
            CanError::Overrun => {
                defmt::warn!("CAN FIFO overrun");
            }
        }
    }
}

// Application-level processing task
#[embassy_executor::task]
async fn application_task() {
    loop {
        // Receive and process messages
        let msg = RX_CHANNEL.receive().await;
        
        match msg.id {
            0x100 => {
                // Process speed message
                let speed = u16::from_le_bytes([msg.data[0], msg.data[1]]);
                defmt::info!("Speed: {} km/h", speed);
            }
            0x200 => {
                // Process temperature
                let temp = i16::from_le_bytes([msg.data[0], msg.data[1]]);
                defmt::info!("Temperature: {} °C", temp);
            }
            _ => {
                defmt::debug!("Unknown message ID: 0x{:X}", msg.id);
            }
        }
        
        // Example: Send response message every 100ms
        Timer::after(Duration::from_millis(100)).await;
        
        let response = CanMessage {
            id: 0x300,
            data: [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08],
            dlc: 8,
            timestamp: Instant::now(),
        };
        
        TX_CHANNEL.send(response).await;
    }
}
```

## Best Practices for Interrupt-Driven CAN

### 1. **Keep ISRs Short and Fast**
- Minimum processing in interrupt context
- Defer heavy work to tasks/threads
- Avoid floating-point operations in ISRs (if FPU context saving is expensive)

### 2. **Use Lock-Free Data Structures**
- Ring buffers for single producer/single consumer
- Atomic operations for shared counters
- Minimize critical sections

### 3. **Proper Priority Assignment**
```
Error Handling (Highest) > RX > TX > Application Processing (Lowest)
```

### 4. **Buffer Sizing**
- Size buffers based on worst-case bus load
- Monitor overflow conditions
- Consider burst traffic patterns

### 5. **Error Recovery Strategies**
```c
// Example error recovery
if (bus_off_detected) {
    disable_can_peripheral();
    delay_ms(100);
    reinitialize_can();
    clear_error_counters();
}
```

### 6. **Timestamping**
- Capture hardware timestamps when available
- Use high-resolution timers (microsecond precision)
- Critical for time-synchronization protocols

### 7. **Testing Considerations**
- Test under maximum bus load
- Inject error conditions (disconnected bus, high error rates)
- Verify FIFO overflow handling
- Measure interrupt latency and jitter

## Summary

**Interrupt-driven CAN handling** provides efficient, low-latency message processing essential for real-time embedded systems. Key takeaways:

- **Interrupts enable immediate response** to CAN events without CPU polling overhead
- **ISRs must be fast** - use ring buffers and defer processing to application tasks
- **Proper prioritization** ensures error handling > RX > TX > application processing
- **Thread-safe data structures** (queues, atomic operations) enable safe communication between ISR and application contexts
- **RTOS integration** (FreeRTOS, RTIC, Embassy) provides sophisticated task scheduling and synchronization
- **Error handling** in interrupts prevents bus-off and overflow conditions from disrupting operation
- **Modern Rust frameworks** (RTIC, Embassy) provide type-safe, zero-cost abstractions for interrupt handling

The choice between bare-metal C, C++ with RTOS, or Rust depends on project requirements, but the fundamental principles of fast ISRs, proper buffering, and priority management remain constant across all implementations.