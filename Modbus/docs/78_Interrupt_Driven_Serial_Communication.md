# Interrupt-Driven Serial Communication in Modbus

## Overview

Interrupt-driven serial communication is a fundamental technique for efficient UART-based data handling in Modbus implementations. Instead of continuously polling the serial port (which wastes CPU cycles), the system uses hardware interrupts to notify the processor when data arrives or when the transmit buffer is ready for more data. This approach is critical for Modbus RTU and ASCII protocols, where timing constraints and deterministic response times are essential.

## Key Concepts

### Why Use Interrupts?

**Polling approach problems:**
- Wastes CPU cycles checking for data that may not be available
- Can miss characters if polling frequency is too low
- Difficult to meet Modbus timing requirements (3.5 character times for frame gaps)

**Interrupt-driven advantages:**
- CPU freed for other tasks while waiting for data
- Immediate response to incoming characters
- Precise timing control for Modbus frame detection
- Efficient handling of transmit buffer availability

### Modbus-Specific Requirements

Modbus RTU requires precise timing:
- **Frame gap**: 3.5 character times of silence marks the end of a frame
- **Character timeout**: 1.5 character times between characters indicates a broken frame
- Interrupts enable accurate measurement of these timing intervals using hardware timers

## C/C++ Implementation

### Basic UART Interrupt Setup (STM32 Example)

```c
#include <stdint.h>
#include <stdbool.h>

#define MODBUS_BUFFER_SIZE 256
#define MODBUS_T35_TIMEOUT 3500  // microseconds for 3.5 char times at 9600 baud

// Modbus receive buffer
typedef struct {
    uint8_t buffer[MODBUS_BUFFER_SIZE];
    volatile uint16_t write_idx;
    volatile uint16_t read_idx;
    volatile bool frame_complete;
} modbus_rx_buffer_t;

// Modbus transmit buffer
typedef struct {
    uint8_t buffer[MODBUS_BUFFER_SIZE];
    volatile uint16_t write_idx;
    volatile uint16_t read_idx;
    volatile bool tx_busy;
} modbus_tx_buffer_t;

static modbus_rx_buffer_t rx_buf = {0};
static modbus_tx_buffer_t tx_buf = {0};
static volatile uint32_t last_rx_time = 0;

// Initialize UART with interrupts
void modbus_uart_init(uint32_t baudrate) {
    // Enable UART clock (platform-specific)
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    
    // Configure UART parameters
    USART2->BRR = SystemCoreClock / baudrate;
    USART2->CR1 = USART_CR1_UE |      // UART Enable
                  USART_CR1_TE |      // Transmitter Enable
                  USART_CR1_RE |      // Receiver Enable
                  USART_CR1_RXNEIE;   // RX interrupt enable
    
    // Enable UART interrupt in NVIC
    NVIC_SetPriority(USART2_IRQn, 0);
    NVIC_EnableIRQ(USART2_IRQn);
    
    // Configure timer for frame gap detection
    modbus_timer_init();
}

// UART interrupt handler
void USART2_IRQHandler(void) {
    uint32_t sr = USART2->SR;
    
    // Handle receive interrupt
    if (sr & USART_SR_RXNE) {
        uint8_t data = USART2->DR;  // Reading DR clears RXNE flag
        
        // Check for buffer overflow
        uint16_t next_idx = (rx_buf.write_idx + 1) % MODBUS_BUFFER_SIZE;
        if (next_idx != rx_buf.read_idx) {
            rx_buf.buffer[rx_buf.write_idx] = data;
            rx_buf.write_idx = next_idx;
            
            // Restart timer for character timeout detection
            last_rx_time = get_microseconds();
            modbus_timer_restart();
        }
    }
    
    // Handle transmit interrupt
    if (sr & USART_SR_TXE) {
        if (tx_buf.read_idx != tx_buf.write_idx) {
            // Send next byte
            USART2->DR = tx_buf.buffer[tx_buf.read_idx];
            tx_buf.read_idx = (tx_buf.read_idx + 1) % MODBUS_BUFFER_SIZE;
        } else {
            // No more data, disable TX interrupt
            USART2->CR1 &= ~USART_CR1_TXEIE;
            tx_buf.tx_busy = false;
        }
    }
    
    // Handle transmission complete
    if (sr & USART_SR_TC) {
        USART2->SR &= ~USART_SR_TC;
        // Switch RS485 transceiver to receive mode if needed
        RS485_RX_MODE();
    }
}

// Timer interrupt for frame gap detection (3.5 character times)
void TIM2_IRQHandler(void) {
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR &= ~TIM_SR_UIF;  // Clear interrupt flag
        
        // Check if enough time has passed for frame gap
        uint32_t elapsed = get_microseconds() - last_rx_time;
        if (elapsed >= MODBUS_T35_TIMEOUT && rx_buf.write_idx != rx_buf.read_idx) {
            rx_buf.frame_complete = true;
            TIM2->CR1 &= ~TIM_CR1_CEN;  // Stop timer
        }
    }
}

// Send Modbus frame
bool modbus_send_frame(const uint8_t *data, uint16_t length) {
    if (tx_buf.tx_busy || length > MODBUS_BUFFER_SIZE) {
        return false;
    }
    
    // Copy data to transmit buffer
    for (uint16_t i = 0; i < length; i++) {
        tx_buf.buffer[i] = data[i];
    }
    tx_buf.write_idx = length;
    tx_buf.read_idx = 0;
    tx_buf.tx_busy = true;
    
    // Switch RS485 to transmit mode
    RS485_TX_MODE();
    
    // Enable TX interrupt to start transmission
    USART2->CR1 |= USART_CR1_TXEIE;
    
    return true;
}

// Receive Modbus frame
bool modbus_receive_frame(uint8_t *data, uint16_t *length) {
    if (!rx_buf.frame_complete) {
        return false;
    }
    
    // Disable interrupts while reading buffer
    __disable_irq();
    
    *length = 0;
    while (rx_buf.read_idx != rx_buf.write_idx) {
        data[*length] = rx_buf.buffer[rx_buf.read_idx];
        rx_buf.read_idx = (rx_buf.read_idx + 1) % MODBUS_BUFFER_SIZE;
        (*length)++;
    }
    
    rx_buf.frame_complete = false;
    
    __enable_irq();
    
    return true;
}
```

### Advanced Implementation with DMA

```c
// DMA-based transmission for better performance
void modbus_uart_init_dma(void) {
    // Enable DMA clock
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    
    // Configure DMA for UART TX (DMA1 Channel 4 for USART2_TX on STM32)
    DMA1_Channel4->CPAR = (uint32_t)&USART2->DR;  // Peripheral address
    DMA1_Channel4->CCR = DMA_CCR_DIR |     // Memory to peripheral
                         DMA_CCR_MINC |     // Memory increment
                         DMA_CCR_TCIE;      // Transfer complete interrupt
    
    NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    
    // Enable UART DMA mode
    USART2->CR3 |= USART_CR3_DMAT;
}

// DMA transfer complete handler
void DMA1_Channel4_IRQHandler(void) {
    if (DMA1->ISR & DMA_ISR_TCIF4) {
        DMA1->IFCR = DMA_IFCR_CTCIF4;  // Clear flag
        tx_buf.tx_busy = false;
        RS485_RX_MODE();
    }
}

// Send using DMA
bool modbus_send_frame_dma(const uint8_t *data, uint16_t length) {
    if (tx_buf.tx_busy) return false;
    
    tx_buf.tx_busy = true;
    RS485_TX_MODE();
    
    DMA1_Channel4->CMAR = (uint32_t)data;     // Memory address
    DMA1_Channel4->CNDTR = length;            // Number of bytes
    DMA1_Channel4->CCR |= DMA_CCR_EN;         // Enable DMA
    
    return true;
}
```

## Rust Implementation

### Using embedded-hal and RTIC (Real-Time Interrupt-driven Concurrency)

```rust
#![no_std]
#![no_main]

use cortex_m::interrupt::Mutex;
use core::cell::RefCell;
use heapless::spsc::{Queue, Producer, Consumer};
use embedded_hal::serial::{Read, Write};

const MODBUS_BUFFER_SIZE: usize = 256;
const T35_TIMEOUT_US: u32 = 3500;

// Shared resources protected by mutex
type RxQueue = Queue<u8, MODBUS_BUFFER_SIZE>;
type TxQueue = Queue<u8, MODBUS_BUFFER_SIZE>;

static RX_PRODUCER: Mutex<RefCell<Option<Producer<u8, MODBUS_BUFFER_SIZE>>>> = 
    Mutex::new(RefCell::new(None));
static TX_CONSUMER: Mutex<RefCell<Option<Consumer<u8, MODBUS_BUFFER_SIZE>>>> = 
    Mutex::new(RefCell::new(None));

// Modbus frame state
#[derive(Debug, Clone, Copy, PartialEq)]
enum ModbusRxState {
    Idle,
    Receiving,
    FrameComplete,
}

struct ModbusSerial<UART> {
    uart: UART,
    rx_queue: Consumer<u8, MODBUS_BUFFER_SIZE>,
    tx_queue: Producer<u8, MODBUS_BUFFER_SIZE>,
    rx_state: ModbusRxState,
    frame_buffer: [u8; MODBUS_BUFFER_SIZE],
    frame_length: usize,
}

impl<UART> ModbusSerial<UART>
where
    UART: Read<u8> + Write<u8>,
{
    pub fn new(
        uart: UART,
        rx_queue: Consumer<u8, MODBUS_BUFFER_SIZE>,
        tx_queue: Producer<u8, MODBUS_BUFFER_SIZE>,
    ) -> Self {
        Self {
            uart,
            rx_queue,
            tx_queue,
            rx_state: ModbusRxState::Idle,
            frame_buffer: [0u8; MODBUS_BUFFER_SIZE],
            frame_length: 0,
        }
    }

    // Called from UART RX interrupt
    pub fn handle_rx_interrupt(&mut self) -> Result<(), ()> {
        match self.uart.read() {
            Ok(byte) => {
                // Enqueue received byte
                cortex_m::interrupt::free(|cs| {
                    if let Some(ref mut producer) = RX_PRODUCER.borrow(cs).borrow_mut().as_mut() {
                        producer.enqueue(byte).ok();
                    }
                });
                
                self.rx_state = ModbusRxState::Receiving;
                Ok(())
            }
            Err(_) => Err(()),
        }
    }

    // Called from UART TX interrupt
    pub fn handle_tx_interrupt(&mut self) -> Result<(), ()> {
        cortex_m::interrupt::free(|cs| {
            if let Some(ref mut consumer) = TX_CONSUMER.borrow(cs).borrow_mut().as_mut() {
                if let Some(byte) = consumer.dequeue() {
                    self.uart.write(byte).ok();
                }
            }
        });
        Ok(())
    }

    // Called from timer interrupt (T3.5 timeout)
    pub fn handle_frame_timeout(&mut self) {
        if self.rx_state == ModbusRxState::Receiving {
            // Move all received bytes to frame buffer
            self.frame_length = 0;
            while let Some(byte) = self.rx_queue.dequeue() {
                if self.frame_length < MODBUS_BUFFER_SIZE {
                    self.frame_buffer[self.frame_length] = byte;
                    self.frame_length += 1;
                }
            }
            
            if self.frame_length > 0 {
                self.rx_state = ModbusRxState::FrameComplete;
            } else {
                self.rx_state = ModbusRxState::Idle;
            }
        }
    }

    // Check if frame is ready
    pub fn frame_ready(&self) -> bool {
        self.rx_state == ModbusRxState::FrameComplete
    }

    // Get received frame
    pub fn get_frame(&mut self) -> Option<&[u8]> {
        if self.rx_state == ModbusRxState::FrameComplete {
            self.rx_state = ModbusRxState::Idle;
            Some(&self.frame_buffer[..self.frame_length])
        } else {
            None
        }
    }

    // Send frame
    pub fn send_frame(&mut self, data: &[u8]) -> Result<(), ()> {
        for &byte in data {
            self.tx_queue.enqueue(byte).map_err(|_| ())?;
        }
        
        // Trigger first TX interrupt
        if let Some(byte) = self.tx_queue.dequeue() {
            self.uart.write(byte).ok();
        }
        
        Ok(())
    }
}

// RTIC application example
#[rtic::app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [EXTI0])]
mod app {
    use super::*;
    use stm32f4xx_hal::{
        pac,
        prelude::*,
        serial::{Config, Serial, Event},
        timer::{Timer, Event as TimerEvent},
    };
    use heapless::spsc::Queue;

    #[shared]
    struct Shared {
        modbus: ModbusSerial<Serial<pac::USART2>>,
    }

    #[local]
    struct Local {
        timer: Timer<pac::TIM2>,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local, init::Monotonics) {
        let dp = ctx.device;
        let rcc = dp.RCC.constrain();
        let clocks = rcc.cfgr.freeze();

        // Setup UART
        let gpioa = dp.GPIOA.split();
        let tx_pin = gpioa.pa2.into_alternate();
        let rx_pin = gpioa.pa3.into_alternate();

        let serial_config = Config::default().baudrate(9600.bps());
        let mut serial = Serial::new(
            dp.USART2,
            (tx_pin, rx_pin),
            serial_config,
            &clocks,
        ).unwrap();

        // Enable UART interrupts
        serial.listen(Event::Rxne);
        serial.listen(Event::Txe);

        // Setup timer for T3.5 timeout
        let mut timer = Timer::new(dp.TIM2, &clocks);
        timer.start(3500.micros());
        timer.listen(TimerEvent::TimeOut);

        // Create queues
        let rx_queue: RxQueue = Queue::new();
        let tx_queue: TxQueue = Queue::new();
        let (rx_prod, rx_cons) = rx_queue.split();
        let (tx_prod, tx_cons) = tx_queue.split();

        // Store producers/consumers in statics
        cortex_m::interrupt::free(|cs| {
            *RX_PRODUCER.borrow(cs).borrow_mut() = Some(rx_prod);
            *TX_CONSUMER.borrow(cs).borrow_mut() = Some(tx_cons);
        });

        let modbus = ModbusSerial::new(serial, rx_cons, tx_prod);

        (
            Shared { modbus },
            Local { timer },
            init::Monotonics(),
        )
    }

    #[task(binds = USART2, shared = [modbus])]
    fn usart2(mut ctx: usart2::Context) {
        ctx.shared.modbus.lock(|modbus| {
            modbus.handle_rx_interrupt().ok();
            modbus.handle_tx_interrupt().ok();
        });
    }

    #[task(binds = TIM2, shared = [modbus], local = [timer])]
    fn tim2(mut ctx: tim2::Context) {
        ctx.local.timer.clear_interrupt(TimerEvent::TimeOut);
        
        ctx.shared.modbus.lock(|modbus| {
            modbus.handle_frame_timeout();
        });
    }
}
```

## Summary

**Interrupt-driven serial communication** is essential for robust Modbus implementations because it:

1. **Improves efficiency**: Frees the CPU from polling, allowing it to handle other tasks
2. **Ensures timing accuracy**: Hardware interrupts provide deterministic response times needed for Modbus frame gap detection (3.5 character times)
3. **Prevents data loss**: Immediate response to incoming characters reduces the risk of buffer overruns
4. **Enables precise protocol compliance**: Timer interrupts accurately measure inter-character and inter-frame gaps

**Key implementation elements:**
- **RX interrupt**: Triggered when data arrives, stores bytes in circular buffer
- **TX interrupt**: Triggered when transmit buffer empty, sends next byte
- **Timer interrupt**: Detects frame boundaries (T3.5 timeout in Modbus RTU)
- **Circular buffers**: Efficient data storage with overflow protection
- **State management**: Tracks receiving/transmitting states and frame completion

**Modern enhancements:**
- **DMA transfers**: Offload data movement from CPU entirely for maximum efficiency
- **RTIC framework** (Rust): Provides safe, zero-cost abstractions for interrupt handling
- **Lock-free queues**: Enable safe data sharing between interrupt and application contexts

This approach is fundamental to industrial Modbus implementations where reliability, timing precision, and efficiency are critical requirements.