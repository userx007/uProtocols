# RTOS Integration for Modbus Stacks

## Overview

RTOS (Real-Time Operating System) integration for Modbus involves implementing Modbus protocol stacks on embedded real-time operating systems like FreeRTOS, Zephyr, ThreadX, and others. This integration enables deterministic, multitasking communication in resource-constrained embedded systems where timing guarantees and concurrent task execution are critical.

## Key Concepts

### Why RTOS for Modbus?

1. **Task Isolation**: Separate Modbus communication from application logic
2. **Deterministic Timing**: Meet strict response time requirements
3. **Concurrent Operations**: Handle multiple Modbus connections or protocols simultaneously
4. **Resource Management**: Efficient use of limited RAM/CPU through scheduling
5. **Priority Management**: Ensure critical tasks aren't blocked by communication

### Common RTOS Architectures for Modbus

- **Dedicated Communication Task**: Single task handling all Modbus operations
- **Multi-Instance Design**: Separate tasks for RTU, TCP, and application layers
- **Event-Driven Model**: Tasks triggered by UART interrupts, timers, or network events
- **Queue-Based Communication**: Inter-task communication using message queues

## FreeRTOS Implementation

### C/C++ Example

```c
// modbus_rtos_freertos.c
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <string.h>

// Modbus RTU frame structure
typedef struct {
    uint8_t slave_address;
    uint8_t function_code;
    uint16_t start_address;
    uint16_t quantity;
    uint8_t data[256];
    uint16_t data_length;
} ModbusFrame_t;

// Modbus context
typedef struct {
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
    SemaphoreHandle_t uart_mutex;
    SemaphoreHandle_t data_mutex;
    uint16_t holding_registers[100];
    uint8_t coils[100];
    TaskHandle_t modbus_task_handle;
} ModbusContext_t;

static ModbusContext_t modbus_ctx;

// CRC16 calculation for Modbus RTU
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

// UART receive interrupt (called from ISR)
void uart_rx_isr_handler(uint8_t byte) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static uint8_t rx_buffer[256];
    static uint16_t rx_index = 0;
    static TickType_t last_byte_time = 0;
    
    TickType_t current_time = xTaskGetTickCountFromISR();
    
    // Modbus RTU: 3.5 character timeout detection (assume 1ms = 1 char at 9600 baud)
    if (current_time - last_byte_time > pdMS_TO_TICKS(4)) {
        rx_index = 0; // Start new frame
    }
    
    rx_buffer[rx_index++] = byte;
    last_byte_time = current_time;
    
    // Minimum frame: SlaveID(1) + FC(1) + Data(2) + CRC(2) = 6 bytes
    if (rx_index >= 6) {
        // Send to queue for processing (non-blocking from ISR)
        ModbusFrame_t frame;
        memcpy(&frame, rx_buffer, rx_index);
        frame.data_length = rx_index;
        
        xQueueSendFromISR(modbus_ctx.rx_queue, &frame, &xHigherPriorityTaskWoken);
        rx_index = 0;
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Process Read Holding Registers (FC 0x03)
void process_read_holding_registers(ModbusFrame_t *request, ModbusFrame_t *response) {
    response->slave_address = request->slave_address;
    response->function_code = request->function_code;
    
    uint16_t start_addr = (request->data[0] << 8) | request->data[1];
    uint16_t quantity = (request->data[2] << 8) | request->data[3];
    
    if (start_addr + quantity > 100) {
        // Illegal data address exception
        response->function_code |= 0x80;
        response->data[0] = 0x02;
        response->data_length = 1;
        return;
    }
    
    response->data[0] = quantity * 2; // Byte count
    
    xSemaphoreTake(modbus_ctx.data_mutex, portMAX_DELAY);
    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t value = modbus_ctx.holding_registers[start_addr + i];
        response->data[1 + i * 2] = (value >> 8) & 0xFF;
        response->data[2 + i * 2] = value & 0xFF;
    }
    xSemaphoreGive(modbus_ctx.data_mutex);
    
    response->data_length = 1 + quantity * 2;
}

// Process Write Single Register (FC 0x06)
void process_write_single_register(ModbusFrame_t *request, ModbusFrame_t *response) {
    uint16_t address = (request->data[0] << 8) | request->data[1];
    uint16_t value = (request->data[2] << 8) | request->data[3];
    
    if (address >= 100) {
        response->function_code |= 0x80;
        response->data[0] = 0x02;
        response->data_length = 1;
        return;
    }
    
    xSemaphoreTake(modbus_ctx.data_mutex, portMAX_DELAY);
    modbus_ctx.holding_registers[address] = value;
    xSemaphoreGive(modbus_ctx.data_mutex);
    
    // Echo request as response
    memcpy(response, request, sizeof(ModbusFrame_t));
}

// Main Modbus processing task
void modbus_task(void *pvParameters) {
    ModbusFrame_t rx_frame, tx_frame;
    uint8_t tx_buffer[256];
    
    while (1) {
        // Wait for incoming frame (with timeout)
        if (xQueueReceive(modbus_ctx.rx_queue, &rx_frame, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            // Verify CRC
            uint16_t received_crc = (rx_frame.data[rx_frame.data_length - 2] << 8) |
                                    rx_frame.data[rx_frame.data_length - 1];
            uint16_t calculated_crc = modbus_crc16((uint8_t*)&rx_frame, rx_frame.data_length - 2);
            
            if (received_crc != calculated_crc) {
                continue; // Ignore frame with bad CRC
            }
            
            // Process based on function code
            memset(&tx_frame, 0, sizeof(tx_frame));
            
            switch (rx_frame.function_code) {
                case 0x03: // Read Holding Registers
                    process_read_holding_registers(&rx_frame, &tx_frame);
                    break;
                    
                case 0x06: // Write Single Register
                    process_write_single_register(&rx_frame, &tx_frame);
                    break;
                    
                default:
                    tx_frame.function_code = rx_frame.function_code | 0x80;
                    tx_frame.data[0] = 0x01; // Illegal function
                    tx_frame.data_length = 1;
                    break;
            }
            
            // Build response frame
            uint16_t tx_len = 0;
            tx_buffer[tx_len++] = tx_frame.slave_address;
            tx_buffer[tx_len++] = tx_frame.function_code;
            memcpy(&tx_buffer[tx_len], tx_frame.data, tx_frame.data_length);
            tx_len += tx_frame.data_length;
            
            // Add CRC
            uint16_t crc = modbus_crc16(tx_buffer, tx_len);
            tx_buffer[tx_len++] = crc & 0xFF;
            tx_buffer[tx_len++] = (crc >> 8) & 0xFF;
            
            // Send response
            xSemaphoreTake(modbus_ctx.uart_mutex, portMAX_DELAY);
            // HAL_UART_Transmit(&huart1, tx_buffer, tx_len, 100);
            xSemaphoreGive(modbus_ctx.uart_mutex);
        }
        
        // Yield to other tasks
        taskYIELD();
    }
}

// Initialization
void modbus_rtos_init(void) {
    // Create queues
    modbus_ctx.rx_queue = xQueueCreate(10, sizeof(ModbusFrame_t));
    modbus_ctx.tx_queue = xQueueCreate(10, sizeof(ModbusFrame_t));
    
    // Create mutexes
    modbus_ctx.uart_mutex = xSemaphoreCreateMutex();
    modbus_ctx.data_mutex = xSemaphoreCreateMutex();
    
    // Initialize data
    memset(modbus_ctx.holding_registers, 0, sizeof(modbus_ctx.holding_registers));
    memset(modbus_ctx.coils, 0, sizeof(modbus_ctx.coils));
    
    // Create Modbus task with high priority
    xTaskCreate(modbus_task, 
                "ModbusTask", 
                512,  // Stack size
                NULL, 
                configMAX_PRIORITIES - 2,  // High priority
                &modbus_ctx.modbus_task_handle);
}
```

## Zephyr RTOS Implementation

### C Example

```c
// modbus_zephyr.c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>

#define MODBUS_STACK_SIZE 2048
#define MODBUS_PRIORITY 5
#define RX_RING_BUF_SIZE 256

// Ring buffer for UART RX
RING_BUF_DECLARE(uart_rx_ringbuf, RX_RING_BUF_SIZE);

// Message queue for Modbus frames
K_MSGQ_DEFINE(modbus_msgq, sizeof(struct modbus_frame), 10, 4);

// Mutex for register access
K_MUTEX_DEFINE(register_mutex);

// Semaphore for UART TX completion
K_SEM_DEFINE(uart_tx_sem, 0, 1);

struct modbus_frame {
    uint8_t data[256];
    uint16_t length;
};

struct modbus_data {
    uint16_t holding_registers[100];
    uint8_t coils[100];
};

static struct modbus_data modbus_data;
static const struct device *uart_dev;

// UART callback
void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data) {
    switch (evt->type) {
        case UART_RX_RDY:
            ring_buf_put(&uart_rx_ringbuf, evt->data.rx.buf + evt->data.rx.offset, 
                        evt->data.rx.len);
            break;
            
        case UART_TX_DONE:
            k_sem_give(&uart_tx_sem);
            break;
            
        default:
            break;
    }
}

// Frame reception thread
void modbus_rx_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    
    struct modbus_frame frame;
    uint8_t byte;
    uint16_t frame_idx = 0;
    int64_t last_byte_time = 0;
    
    while (1) {
        if (ring_buf_get(&uart_rx_ringbuf, &byte, 1) > 0) {
            int64_t current_time = k_uptime_get();
            
            // 3.5 character timeout
            if (current_time - last_byte_time > 4) {
                frame_idx = 0;
            }
            
            frame.data[frame_idx++] = byte;
            last_byte_time = current_time;
            
            if (frame_idx >= 6 && frame_idx < 256) {
                // Potentially complete frame
                frame.length = frame_idx;
                k_msgq_put(&modbus_msgq, &frame, K_NO_WAIT);
                frame_idx = 0;
            }
        } else {
            k_sleep(K_MSEC(1));
        }
    }
}

// Modbus processing thread
void modbus_process_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    
    struct modbus_frame frame;
    uint8_t response[256];
    
    while (1) {
        if (k_msgq_get(&modbus_msgq, &frame, K_MSEC(100)) == 0) {
            uint8_t slave_id = frame.data[0];
            uint8_t function_code = frame.data[1];
            uint16_t resp_len = 0;
            
            response[resp_len++] = slave_id;
            response[resp_len++] = function_code;
            
            switch (function_code) {
                case 0x03: { // Read Holding Registers
                    uint16_t start_addr = (frame.data[2] << 8) | frame.data[3];
                    uint16_t quantity = (frame.data[4] << 8) | frame.data[5];
                    
                    response[resp_len++] = quantity * 2;
                    
                    k_mutex_lock(&register_mutex, K_FOREVER);
                    for (uint16_t i = 0; i < quantity; i++) {
                        uint16_t value = modbus_data.holding_registers[start_addr + i];
                        response[resp_len++] = (value >> 8) & 0xFF;
                        response[resp_len++] = value & 0xFF;
                    }
                    k_mutex_unlock(&register_mutex);
                    break;
                }
                
                case 0x06: { // Write Single Register
                    uint16_t address = (frame.data[2] << 8) | frame.data[3];
                    uint16_t value = (frame.data[4] << 8) | frame.data[5];
                    
                    k_mutex_lock(&register_mutex, K_FOREVER);
                    modbus_data.holding_registers[address] = value;
                    k_mutex_unlock(&register_mutex);
                    
                    memcpy(&response[resp_len], &frame.data[2], 4);
                    resp_len += 4;
                    break;
                }
            }
            
            // Send response via UART
            uart_tx(uart_dev, response, resp_len, SYS_FOREVER_US);
            k_sem_take(&uart_tx_sem, K_FOREVER);
        }
    }
}

// Thread definitions
K_THREAD_DEFINE(modbus_rx_tid, MODBUS_STACK_SIZE, modbus_rx_thread, 
                NULL, NULL, NULL, MODBUS_PRIORITY, 0, 0);

K_THREAD_DEFINE(modbus_process_tid, MODBUS_STACK_SIZE, modbus_process_thread,
                NULL, NULL, NULL, MODBUS_PRIORITY + 1, 0, 0);
```

## Rust Implementation (with Embassy RTOS)

```rust
// modbus_embassy.rs
#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_sync::mutex::Mutex;
use embassy_time::{Duration, Timer, Instant};
use embassy_stm32::usart::{Uart, Config as UartConfig};
use embassy_stm32::peripherals;

// Modbus frame structure
#[derive(Clone, Copy)]
pub struct ModbusFrame {
    data: [u8; 256],
    length: usize,
}

impl ModbusFrame {
    pub fn new() -> Self {
        Self {
            data: [0; 256],
            length: 0,
        }
    }
}

// Modbus data storage
pub struct ModbusData {
    holding_registers: [u16; 100],
    coils: [u8; 100],
}

impl ModbusData {
    pub fn new() -> Self {
        Self {
            holding_registers: [0; 100],
            coils: [0; 100],
        }
    }
}

// Shared resources
static MODBUS_DATA: Mutex<CriticalSectionRawMutex, ModbusData> = 
    Mutex::new(ModbusData::new());

static FRAME_CHANNEL: Channel<CriticalSectionRawMutex, ModbusFrame, 10> = 
    Channel::new();

// CRC16 calculation
fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for byte in data {
        crc ^= *byte as u16;
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

// UART reception task
#[embassy_executor::task]
async fn modbus_rx_task(mut uart: Uart<'static, peripherals::USART1>) {
    let mut rx_buffer = [0u8; 256];
    let mut frame_buffer = [0u8; 256];
    let mut frame_idx = 0;
    let mut last_byte_time = Instant::now();
    
    loop {
        match uart.read(&mut rx_buffer[..1]).await {
            Ok(_) => {
                let current_time = Instant::now();
                let elapsed = current_time.duration_since(last_byte_time);
                
                // 3.5 character timeout (4ms at 9600 baud)
                if elapsed > Duration::from_millis(4) {
                    frame_idx = 0;
                }
                
                frame_buffer[frame_idx] = rx_buffer[0];
                frame_idx += 1;
                last_byte_time = current_time;
                
                // Check if we have a potentially complete frame
                if frame_idx >= 6 {
                    let mut frame = ModbusFrame::new();
                    frame.data[..frame_idx].copy_from_slice(&frame_buffer[..frame_idx]);
                    frame.length = frame_idx;
                    
                    // Send to processing channel
                    FRAME_CHANNEL.send(frame).await;
                    frame_idx = 0;
                }
            }
            Err(_) => {
                Timer::after(Duration::from_millis(1)).await;
            }
        }
    }
}

// Modbus processing task
#[embassy_executor::task]
async fn modbus_process_task(mut uart: Uart<'static, peripherals::USART2>) {
    loop {
        let frame = FRAME_CHANNEL.receive().await;
        
        if frame.length < 6 {
            continue;
        }
        
        // Verify CRC
        let received_crc = u16::from_le_bytes([
            frame.data[frame.length - 2],
            frame.data[frame.length - 1],
        ]);
        let calculated_crc = modbus_crc16(&frame.data[..frame.length - 2]);
        
        if received_crc != calculated_crc {
            continue;
        }
        
        let slave_id = frame.data[0];
        let function_code = frame.data[1];
        
        let mut response = [0u8; 256];
        let mut resp_len = 0;
        
        response[resp_len] = slave_id;
        resp_len += 1;
        response[resp_len] = function_code;
        resp_len += 1;
        
        match function_code {
            0x03 => { // Read Holding Registers
                let start_addr = u16::from_be_bytes([frame.data[2], frame.data[3]]) as usize;
                let quantity = u16::from_be_bytes([frame.data[4], frame.data[5]]) as usize;
                
                response[resp_len] = (quantity * 2) as u8;
                resp_len += 1;
                
                let mut data = MODBUS_DATA.lock().await;
                for i in 0..quantity {
                    let value = data.holding_registers[start_addr + i];
                    response[resp_len] = (value >> 8) as u8;
                    resp_len += 1;
                    response[resp_len] = (value & 0xFF) as u8;
                    resp_len += 1;
                }
            }
            
            0x06 => { // Write Single Register
                let address = u16::from_be_bytes([frame.data[2], frame.data[3]]) as usize;
                let value = u16::from_be_bytes([frame.data[4], frame.data[5]]);
                
                let mut data = MODBUS_DATA.lock().await;
                data.holding_registers[address] = value;
                
                response[resp_len..resp_len + 4].copy_from_slice(&frame.data[2..6]);
                resp_len += 4;
            }
            
            _ => {
                response[1] = function_code | 0x80;
                response[resp_len] = 0x01; // Illegal function
                resp_len += 1;
            }
        }
        
        // Add CRC
        let crc = modbus_crc16(&response[..resp_len]);
        response[resp_len] = (crc & 0xFF) as u8;
        resp_len += 1;
        response[resp_len] = (crc >> 8) as u8;
        resp_len += 1;
        
        // Send response
        let _ = uart.write(&response[..resp_len]).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    // Initialize hardware and spawn tasks
    // spawner.spawn(modbus_rx_task(uart_rx)).unwrap();
    // spawner.spawn(modbus_process_task(uart_tx)).unwrap();
}
```

## Summary

**RTOS integration for Modbus** enables robust, real-time communication in embedded systems by:

- **Providing task isolation** between communication and application logic
- **Ensuring deterministic response times** through priority-based scheduling
- **Managing concurrent operations** via queues, semaphores, and mutexes
- **Optimizing resource usage** in memory-constrained environments

Key implementation patterns include dedicated communication tasks, interrupt-driven reception with frame assembly, mutex-protected shared data structures, and inter-task communication via message queues. Popular platforms like **FreeRTOS** (bare-metal focus), **Zephyr** (comprehensive driver ecosystem), and **Embassy** (async Rust) each offer different trade-offs in memory footprint, ease of use, and safety guarantees. Proper RTOS integration is essential for industrial applications requiring reliable, real-time Modbus communication alongside complex application processing.