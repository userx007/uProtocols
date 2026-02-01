# DMA for Serial Transfers in Modbus Communication

## Overview

Direct Memory Access (DMA) is a hardware feature that allows peripheral devices to transfer data directly to/from memory without CPU intervention. In Modbus serial communication, DMA significantly improves performance by offloading the byte-by-byte handling of UART data, reducing CPU overhead, and enabling higher baud rates with minimal latency.

## Why DMA Matters for Modbus

Traditional interrupt-driven serial communication requires the CPU to handle each byte individually:
- **Interrupt overhead**: Each byte triggers an interrupt, context switch, and ISR execution
- **CPU saturation**: At high baud rates (115200+), interrupt frequency can consume significant CPU cycles
- **Latency issues**: Other tasks may be delayed during heavy serial traffic
- **Buffer management**: Manual copying between peripheral and application buffers

DMA eliminates these issues by autonomously transferring blocks of data, freeing the CPU for protocol processing and application logic.

## Key Concepts

### DMA Transfer Modes
1. **Normal Mode**: Single transfer, then stops (requires re-initialization)
2. **Circular Mode**: Continuous buffer wrapping for RX (ideal for streaming data)
3. **Memory-to-Peripheral**: For TX operations
4. **Peripheral-to-Memory**: For RX operations

### DMA Configuration Requirements
- **Source/Destination addresses**: UART data register and memory buffer
- **Transfer size**: Number of bytes to transfer
- **Priority level**: Resolves conflicts when multiple DMA channels compete
- **Interrupt enables**: Transfer complete, half-complete, error notifications

---

## C/C++ Implementation (STM32 Example)

This example uses STM32 HAL for UART DMA with Modbus RTU:

```c
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <string.h>

// Modbus buffer configuration
#define MODBUS_RX_BUFFER_SIZE 256
#define MODBUS_TX_BUFFER_SIZE 256

// DMA circular buffers
static uint8_t dma_rx_buffer[MODBUS_RX_BUFFER_SIZE];
static uint8_t dma_tx_buffer[MODBUS_TX_BUFFER_SIZE];

// Modbus frame parser state
typedef struct {
    uint8_t frame_buffer[MODBUS_RX_BUFFER_SIZE];
    uint16_t frame_length;
    uint32_t last_rx_time;
    volatile uint16_t dma_write_ptr;
    uint16_t read_ptr;
} modbus_dma_context_t;

static modbus_dma_context_t modbus_ctx = {0};

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_tx;
DMA_HandleTypeDef hdma_usart1_rx;

// Initialize UART with DMA
void modbus_dma_init(void) {
    // UART configuration
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    
    HAL_UART_Init(&huart1);
    
    // Start DMA reception in circular mode
    HAL_UART_Receive_DMA(&huart1, dma_rx_buffer, MODBUS_RX_BUFFER_SIZE);
}

// Get current DMA write position
static inline uint16_t get_dma_write_position(void) {
    return MODBUS_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx);
}

// Check for complete Modbus frame (3.5 character timeout)
void modbus_dma_process(void) {
    uint16_t current_write_ptr = get_dma_write_position();
    uint32_t current_time = HAL_GetTick();
    
    // Check if new data arrived
    if (current_write_ptr != modbus_ctx.dma_write_ptr) {
        modbus_ctx.last_rx_time = current_time;
        modbus_ctx.dma_write_ptr = current_write_ptr;
        return; // Still receiving
    }
    
    // Check for frame timeout (3.5 chars at 115200 = ~0.3ms, use 2ms margin)
    if ((current_time - modbus_ctx.last_rx_time) < 2) {
        return; // Not timed out yet
    }
    
    // Calculate received bytes
    uint16_t bytes_available;
    if (current_write_ptr >= modbus_ctx.read_ptr) {
        bytes_available = current_write_ptr - modbus_ctx.read_ptr;
    } else {
        bytes_available = MODBUS_RX_BUFFER_SIZE - modbus_ctx.read_ptr + current_write_ptr;
    }
    
    if (bytes_available == 0) return;
    
    // Copy frame from circular buffer
    modbus_ctx.frame_length = 0;
    for (uint16_t i = 0; i < bytes_available; i++) {
        uint16_t pos = (modbus_ctx.read_ptr + i) % MODBUS_RX_BUFFER_SIZE;
        modbus_ctx.frame_buffer[modbus_ctx.frame_length++] = dma_rx_buffer[pos];
    }
    
    modbus_ctx.read_ptr = current_write_ptr;
    
    // Process Modbus frame
    modbus_process_frame(modbus_ctx.frame_buffer, modbus_ctx.frame_length);
}

// Send Modbus response using DMA
void modbus_dma_transmit(const uint8_t *data, uint16_t length) {
    // Copy to DMA buffer
    memcpy(dma_tx_buffer, data, length);
    
    // Transmit via DMA (non-blocking)
    HAL_UART_Transmit_DMA(&huart1, dma_tx_buffer, length);
}

// DMA TX complete callback
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        // Optional: Signal transmission complete
        // Can switch RS485 transceiver back to RX mode
    }
}

// Example Modbus frame processor
void modbus_process_frame(uint8_t *frame, uint16_t length) {
    if (length < 4) return; // Minimum Modbus frame size
    
    uint8_t slave_addr = frame[0];
    uint8_t function_code = frame[1];
    
    // CRC check
    uint16_t received_crc = (frame[length-1] << 8) | frame[length-2];
    uint16_t calculated_crc = modbus_crc16(frame, length - 2);
    
    if (received_crc != calculated_crc) {
        return; // Invalid CRC
    }
    
    // Process based on function code
    uint8_t response[256];
    uint16_t response_len = modbus_handle_request(frame, length, response);
    
    if (response_len > 0) {
        modbus_dma_transmit(response, response_len);
    }
}

// Simple CRC-16 Modbus
uint16_t modbus_crc16(const uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
```

---

## Rust Implementation (embedded-hal)

Using `embedded-hal` traits with DMA support (cortex-m example):

```rust
#![no_std]

use core::sync::atomic::{AtomicU16, Ordering};
use embedded_hal::serial;
use cortex_m::interrupt::Mutex;
use stm32f4xx_hal::{
    dma::{Stream2, Stream7, StreamsTuple, Transfer},
    pac::{DMA2, USART1},
    prelude::*,
    serial::{Config, Serial, Tx, Rx},
};

const RX_BUFFER_SIZE: usize = 256;
const TX_BUFFER_SIZE: usize = 256;
const FRAME_TIMEOUT_MS: u32 = 2;

// DMA buffers (must be 'static for DMA)
static mut RX_BUFFER: [u8; RX_BUFFER_SIZE] = [0; RX_BUFFER_SIZE];
static mut TX_BUFFER: [u8; TX_BUFFER_SIZE] = [0; TX_BUFFER_SIZE];

// Modbus frame state
pub struct ModbusDma {
    rx_transfer: Option<Transfer<Stream2<DMA2>, Rx<USART1>, &'static mut [u8]>>,
    tx_transfer: Option<Transfer<Stream7<DMA2>, Tx<USART1>, &'static mut [u8]>>,
    frame_buffer: [u8; RX_BUFFER_SIZE],
    read_ptr: u16,
    last_rx_time: u32,
}

impl ModbusDma {
    pub fn new(
        usart: USART1,
        tx_pin: impl Into<stm32f4xx_hal::gpio::Alternate<7>>,
        rx_pin: impl Into<stm32f4xx_hal::gpio::Alternate<7>>,
        dma: DMA2,
        clocks: &stm32f4xx_hal::rcc::Clocks,
    ) -> Self {
        // Configure USART
        let serial = Serial::new(
            usart,
            (tx_pin.into(), rx_pin.into()),
            Config::default().baudrate(115200.bps()),
            clocks,
        ).unwrap();

        let (tx, rx) = serial.split();
        
        // Setup DMA streams
        let streams = StreamsTuple::new(dma);
        
        // RX DMA: Peripheral-to-Memory, Circular
        let rx_buffer = unsafe { &mut RX_BUFFER };
        let rx_transfer = Transfer::init_peripheral_to_memory(
            streams.2,
            rx,
            rx_buffer,
            None,
            stm32f4xx_hal::dma::config::DmaConfig::default()
                .memory_increment(true)
                .circular_buffer(true),
        );
        rx_transfer.start(|_rx| {});

        // TX DMA: Memory-to-Peripheral
        let tx_buffer = unsafe { &mut TX_BUFFER };
        let tx_transfer = Transfer::init_memory_to_peripheral(
            streams.7,
            tx,
            tx_buffer,
            None,
            stm32f4xx_hal::dma::config::DmaConfig::default()
                .memory_increment(true),
        );

        ModbusDma {
            rx_transfer: Some(rx_transfer),
            tx_transfer: Some(tx_transfer),
            frame_buffer: [0; RX_BUFFER_SIZE],
            read_ptr: 0,
            last_rx_time: 0,
        }
    }

    // Get current DMA write position
    fn get_write_position(&self) -> u16 {
        if let Some(ref transfer) = self.rx_transfer {
            RX_BUFFER_SIZE as u16 - transfer.number_of_transfers() as u16
        } else {
            0
        }
    }

    // Process received data with timeout detection
    pub fn process(&mut self, current_time_ms: u32) -> Option<&[u8]> {
        let write_ptr = self.get_write_position();
        
        // Check if new data arrived
        if write_ptr != self.read_ptr {
            self.last_rx_time = current_time_ms;
            return None; // Still receiving
        }

        // Check timeout
        if (current_time_ms - self.last_rx_time) < FRAME_TIMEOUT_MS {
            return None;
        }

        // Calculate available bytes
        let bytes_available = if write_ptr >= self.read_ptr {
            write_ptr - self.read_ptr
        } else {
            RX_BUFFER_SIZE as u16 - self.read_ptr + write_ptr
        };

        if bytes_available == 0 {
            return None;
        }

        // Copy from circular buffer
        let rx_buf = unsafe { &RX_BUFFER };
        let mut frame_len = 0;
        
        for i in 0..bytes_available {
            let pos = ((self.read_ptr + i) % RX_BUFFER_SIZE as u16) as usize;
            self.frame_buffer[frame_len] = rx_buf[pos];
            frame_len += 1;
        }

        self.read_ptr = write_ptr;
        Some(&self.frame_buffer[..frame_len])
    }

    // Transmit Modbus frame via DMA
    pub fn transmit(&mut self, data: &[u8]) -> Result<(), ()> {
        if data.len() > TX_BUFFER_SIZE {
            return Err(());
        }

        // Wait for previous transfer to complete
        if let Some(ref transfer) = self.tx_transfer {
            if !transfer.is_complete() {
                return Err(());
            }
        }

        // Copy to DMA buffer
        let tx_buf = unsafe { &mut TX_BUFFER };
        tx_buf[..data.len()].copy_from_slice(data);

        // Start DMA transfer
        if let Some(mut transfer) = self.tx_transfer.take() {
            transfer.set_number_of_transfers(data.len() as u16);
            transfer.start(|_tx| {});
            self.tx_transfer = Some(transfer);
            Ok(())
        } else {
            Err(())
        }
    }
}

// CRC-16 Modbus calculation
pub fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

// Example usage
pub fn modbus_example(modbus: &mut ModbusDma, systick_ms: u32) {
    if let Some(frame) = modbus.process(systick_ms) {
        if frame.len() >= 4 {
            // Verify CRC
            let payload_len = frame.len() - 2;
            let received_crc = u16::from_le_bytes([frame[payload_len], frame[payload_len + 1]]);
            let calculated_crc = modbus_crc16(&frame[..payload_len]);
            
            if received_crc == calculated_crc {
                // Process valid frame
                let slave_addr = frame[0];
                let function_code = frame[1];
                
                // Build response (example: echo with new CRC)
                let mut response = [0u8; 256];
                response[..payload_len].copy_from_slice(&frame[..payload_len]);
                let response_crc = modbus_crc16(&response[..payload_len]);
                response[payload_len..payload_len+2].copy_from_slice(&response_crc.to_le_bytes());
                
                let _ = modbus.transmit(&response[..payload_len + 2]);
            }
        }
    }
}
```

---

## Summary

**DMA for serial Modbus transfers** eliminates CPU bottlenecks by autonomously moving data between UART peripherals and memory buffers. Key benefits include:

- **Reduced CPU overhead**: No per-byte interrupts; CPU only handles complete frames
- **Higher throughput**: Supports baud rates up to 1Mbps+ without saturation
- **Lower latency**: Deterministic response times for real-time systems
- **Circular buffering**: Automatic handling of continuous RX streams without data loss

**Implementation essentials:**
1. Configure DMA in circular mode for RX, normal mode for TX
2. Use frame timeout detection (3.5 character times) to identify complete Modbus frames
3. Handle circular buffer wrap-around when extracting frames
4. Maintain separate read/write pointers for buffer management
5. Validate CRC before processing frames

DMA is especially critical in industrial applications with multiple Modbus slaves, high-frequency polling, or concurrent protocol handling (Modbus + CAN, Ethernet, etc.). The examples demonstrate production-ready patterns for STM32 microcontrollers using both C/HAL and Rust embedded ecosystems.