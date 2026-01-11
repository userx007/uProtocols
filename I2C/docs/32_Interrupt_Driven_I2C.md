# Interrupt-Driven I2C: Non-blocking Operations with ISRs

## Overview

Interrupt-driven I2C is a technique that allows your microcontroller to perform I2C communication without blocking the main program execution. Instead of polling status registers or waiting for transfers to complete, the I2C peripheral triggers hardware interrupts when events occur (transfer complete, data received, errors, etc.). This approach is essential for responsive, real-time embedded systems.

## Why Use Interrupt-Driven I2C?

**Advantages:**
- **Non-blocking execution**: Your main program continues running while I2C transfers happen in the background
- **Better CPU utilization**: The processor can perform other tasks instead of busy-waiting
- **Improved responsiveness**: Critical tasks aren't delayed by I2C communication
- **Lower power consumption**: CPU can enter sleep modes between I2C events

**When to use it:**
- Multi-sensor systems where you need to read from multiple devices
- Applications requiring precise timing for other operations
- Battery-powered devices needing efficient power management
- Systems with multiple communication interfaces running concurrently

## Key Concepts

### State Machine Architecture
Interrupt-driven I2C typically uses a state machine to track the progress of multi-byte transactions:
- **IDLE**: No transfer in progress
- **TX_ADDR**: Transmitting device address
- **TX_DATA**: Transmitting data bytes
- **RX_DATA**: Receiving data bytes
- **STOP**: Generating stop condition
- **ERROR**: Handling error conditions

### Interrupt Sources
Common I2C interrupts include:
- **Start/Stop condition generated**
- **Address sent/matched**
- **Byte transmission complete**
- **Byte received**
- **Transfer complete**
- **Arbitration lost**
- **NACK received**
- **Bus error**

## C/C++ Implementation Examples

### Example 1: STM32 HAL-Based Implementation

```c
// i2c_interrupt.h
#ifndef I2C_INTERRUPT_H
#define I2C_INTERRUPT_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

// I2C transaction state
typedef enum {
    I2C_STATE_IDLE,
    I2C_STATE_TX_BUSY,
    I2C_STATE_RX_BUSY,
    I2C_STATE_COMPLETE,
    I2C_STATE_ERROR
} I2C_State_t;

// I2C transaction structure
typedef struct {
    uint8_t device_addr;
    uint8_t *tx_buffer;
    uint8_t *rx_buffer;
    uint16_t tx_size;
    uint16_t rx_size;
    uint16_t tx_count;
    uint16_t rx_count;
    I2C_State_t state;
    void (*callback)(I2C_State_t state);
} I2C_Transaction_t;

// Function prototypes
void I2C_Interrupt_Init(I2C_HandleTypeDef *hi2c);
bool I2C_WriteAsync(uint8_t addr, uint8_t *data, uint16_t size, void (*callback)(I2C_State_t));
bool I2C_ReadAsync(uint8_t addr, uint8_t *data, uint16_t size, void (*callback)(I2C_State_t));
bool I2C_WriteReadAsync(uint8_t addr, uint8_t *tx_data, uint16_t tx_size, 
                        uint8_t *rx_data, uint16_t rx_size, void (*callback)(I2C_State_t));
I2C_State_t I2C_GetState(void);

#endif // I2C_INTERRUPT_H
```

```c
// i2c_interrupt.c
#include "i2c_interrupt.h"
#include <string.h>

static I2C_HandleTypeDef *g_hi2c;
static I2C_Transaction_t g_transaction;

void I2C_Interrupt_Init(I2C_HandleTypeDef *hi2c) {
    g_hi2c = hi2c;
    memset(&g_transaction, 0, sizeof(g_transaction));
    g_transaction.state = I2C_STATE_IDLE;
}

bool I2C_WriteAsync(uint8_t addr, uint8_t *data, uint16_t size, void (*callback)(I2C_State_t)) {
    if (g_transaction.state != I2C_STATE_IDLE) {
        return false; // Busy
    }
    
    g_transaction.device_addr = addr;
    g_transaction.tx_buffer = data;
    g_transaction.tx_size = size;
    g_transaction.tx_count = 0;
    g_transaction.rx_buffer = NULL;
    g_transaction.rx_size = 0;
    g_transaction.callback = callback;
    g_transaction.state = I2C_STATE_TX_BUSY;
    
    // Start I2C transfer with interrupts
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_IT(g_hi2c, addr << 1, data, size);
    
    if (status != HAL_OK) {
        g_transaction.state = I2C_STATE_ERROR;
        return false;
    }
    
    return true;
}

bool I2C_ReadAsync(uint8_t addr, uint8_t *data, uint16_t size, void (*callback)(I2C_State_t)) {
    if (g_transaction.state != I2C_STATE_IDLE) {
        return false;
    }
    
    g_transaction.device_addr = addr;
    g_transaction.tx_buffer = NULL;
    g_transaction.tx_size = 0;
    g_transaction.rx_buffer = data;
    g_transaction.rx_size = size;
    g_transaction.rx_count = 0;
    g_transaction.callback = callback;
    g_transaction.state = I2C_STATE_RX_BUSY;
    
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive_IT(g_hi2c, addr << 1, data, size);
    
    if (status != HAL_OK) {
        g_transaction.state = I2C_STATE_ERROR;
        return false;
    }
    
    return true;
}

bool I2C_WriteReadAsync(uint8_t addr, uint8_t *tx_data, uint16_t tx_size,
                        uint8_t *rx_data, uint16_t rx_size, void (*callback)(I2C_State_t)) {
    if (g_transaction.state != I2C_STATE_IDLE) {
        return false;
    }
    
    g_transaction.device_addr = addr;
    g_transaction.tx_buffer = tx_data;
    g_transaction.tx_size = tx_size;
    g_transaction.rx_buffer = rx_data;
    g_transaction.rx_size = rx_size;
    g_transaction.tx_count = 0;
    g_transaction.rx_count = 0;
    g_transaction.callback = callback;
    g_transaction.state = I2C_STATE_TX_BUSY;
    
    // Use sequential transfer with restart
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read_IT(g_hi2c, addr << 1, 
                                                     tx_data[0], 1, rx_data, rx_size);
    
    if (status != HAL_OK) {
        g_transaction.state = I2C_STATE_ERROR;
        return false;
    }
    
    return true;
}

I2C_State_t I2C_GetState(void) {
    return g_transaction.state;
}

// HAL Callback: Transfer complete
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == g_hi2c) {
        g_transaction.state = I2C_STATE_COMPLETE;
        if (g_transaction.callback) {
            g_transaction.callback(I2C_STATE_COMPLETE);
        }
        g_transaction.state = I2C_STATE_IDLE;
    }
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == g_hi2c) {
        g_transaction.state = I2C_STATE_COMPLETE;
        if (g_transaction.callback) {
            g_transaction.callback(I2C_STATE_COMPLETE);
        }
        g_transaction.state = I2C_STATE_IDLE;
    }
}

// HAL Callback: Error occurred
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == g_hi2c) {
        g_transaction.state = I2C_STATE_ERROR;
        if (g_transaction.callback) {
            g_transaction.callback(I2C_STATE_ERROR);
        }
        g_transaction.state = I2C_STATE_IDLE;
    }
}
```

```c
// main.c - Usage example
#include "i2c_interrupt.h"
#include "stm32f4xx_hal.h"

I2C_HandleTypeDef hi2c1;
uint8_t sensor_data[6];
volatile bool transfer_complete = false;

void transfer_callback(I2C_State_t state) {
    if (state == I2C_STATE_COMPLETE) {
        transfer_complete = true;
        // Process sensor_data here
    } else if (state == I2C_STATE_ERROR) {
        // Handle error
    }
}

int main(void) {
    HAL_Init();
    // Configure I2C peripheral (clocks, GPIO, etc.)
    I2C_Interrupt_Init(&hi2c1);
    
    while (1) {
        // Read from accelerometer at 0x68
        if (I2C_ReadAsync(0x68, sensor_data, 6, transfer_callback)) {
            // Transfer started successfully
            // Do other work while transfer happens
            
            // Wait for completion if needed
            while (!transfer_complete) {
                // Could enter low-power mode here
                __WFI(); // Wait for interrupt
            }
            transfer_complete = false;
            
            // Process data
            int16_t accel_x = (sensor_data[0] << 8) | sensor_data[1];
            int16_t accel_y = (sensor_data[2] << 8) | sensor_data[3];
            int16_t accel_z = (sensor_data[4] << 8) | sensor_data[5];
        }
        
        HAL_Delay(100);
    }
}
```

### Example 2: Low-Level Register-Based Implementation (AVR)

```c
// i2c_interrupt_avr.h
#ifndef I2C_INTERRUPT_AVR_H
#define I2C_INTERRUPT_AVR_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    I2C_IDLE,
    I2C_START,
    I2C_REPEATED_START,
    I2C_ADDR_WRITE,
    I2C_ADDR_READ,
    I2C_WRITE_DATA,
    I2C_READ_DATA,
    I2C_READ_DATA_ACK,
    I2C_STOP,
    I2C_ERROR
} I2C_ISR_State_t;

void i2c_init(uint32_t scl_freq);
bool i2c_write_async(uint8_t addr, const uint8_t *data, uint8_t len);
bool i2c_read_async(uint8_t addr, uint8_t *data, uint8_t len);
bool i2c_is_busy(void);

#endif
```

```c
// i2c_interrupt_avr.c
#include "i2c_interrupt_avr.h"

// TWI Status codes
#define TW_START         0x08
#define TW_REP_START     0x10
#define TW_MT_SLA_ACK    0x18
#define TW_MT_SLA_NACK   0x20
#define TW_MT_DATA_ACK   0x28
#define TW_MT_DATA_NACK  0x30
#define TW_MR_SLA_ACK    0x40
#define TW_MR_SLA_NACK   0x48
#define TW_MR_DATA_ACK   0x50
#define TW_MR_DATA_NACK  0x58

static volatile I2C_ISR_State_t isr_state = I2C_IDLE;
static volatile uint8_t device_addr;
static volatile uint8_t *data_ptr;
static volatile uint8_t data_len;
static volatile uint8_t data_idx;
static volatile bool is_read;

void i2c_init(uint32_t scl_freq) {
    // Set SCL frequency
    // SCL = F_CPU / (16 + 2 * TWBR * Prescaler)
    uint8_t prescaler = 1;
    TWSR = 0; // Prescaler = 1
    TWBR = ((F_CPU / scl_freq) - 16) / 2;
    
    // Enable TWI and interrupt
    TWCR = (1 << TWEN) | (1 << TWIE);
    
    sei(); // Enable global interrupts
}

bool i2c_write_async(uint8_t addr, const uint8_t *data, uint8_t len) {
    if (isr_state != I2C_IDLE) {
        return false;
    }
    
    device_addr = addr << 1; // Write address
    data_ptr = (uint8_t *)data;
    data_len = len;
    data_idx = 0;
    is_read = false;
    isr_state = I2C_START;
    
    // Send START condition
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN) | (1 << TWIE);
    
    return true;
}

bool i2c_read_async(uint8_t addr, uint8_t *data, uint8_t len) {
    if (isr_state != I2C_IDLE) {
        return false;
    }
    
    device_addr = (addr << 1) | 0x01; // Read address
    data_ptr = data;
    data_len = len;
    data_idx = 0;
    is_read = true;
    isr_state = I2C_START;
    
    // Send START condition
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN) | (1 << TWIE);
    
    return true;
}

bool i2c_is_busy(void) {
    return (isr_state != I2C_IDLE);
}

// TWI Interrupt Service Routine
ISR(TWI_vect) {
    uint8_t status = TWSR & 0xF8;
    
    switch (isr_state) {
        case I2C_START:
            if (status == TW_START || status == TW_REP_START) {
                // Send device address
                TWDR = device_addr;
                TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
                isr_state = is_read ? I2C_ADDR_READ : I2C_ADDR_WRITE;
            } else {
                isr_state = I2C_ERROR;
                TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
            }
            break;
            
        case I2C_ADDR_WRITE:
            if (status == TW_MT_SLA_ACK) {
                if (data_idx < data_len) {
                    // Send data byte
                    TWDR = data_ptr[data_idx++];
                    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
                    isr_state = I2C_WRITE_DATA;
                } else {
                    // No data, send STOP
                    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
                    isr_state = I2C_IDLE;
                }
            } else {
                isr_state = I2C_ERROR;
                TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
            }
            break;
            
        case I2C_WRITE_DATA:
            if (status == TW_MT_DATA_ACK) {
                if (data_idx < data_len) {
                    // Send next byte
                    TWDR = data_ptr[data_idx++];
                    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
                } else {
                    // All data sent, send STOP
                    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
                    isr_state = I2C_IDLE;
                }
            } else {
                isr_state = I2C_ERROR;
                TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
            }
            break;
            
        case I2C_ADDR_READ:
            if (status == TW_MR_SLA_ACK) {
                if (data_len > 1) {
                    // More than one byte to read, send ACK after receive
                    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE) | (1 << TWEA);
                } else {
                    // Only one byte, send NACK
                    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
                }
                isr_state = I2C_READ_DATA;
            } else {
                isr_state = I2C_ERROR;
                TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
            }
            break;
            
        case I2C_READ_DATA:
            if (status == TW_MR_DATA_ACK) {
                data_ptr[data_idx++] = TWDR;
                if (data_idx < data_len - 1) {
                    // More bytes to read, send ACK
                    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE) | (1 << TWEA);
                } else {
                    // Last byte next, send NACK
                    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
                }
            } else if (status == TW_MR_DATA_NACK) {
                // Last byte received
                data_ptr[data_idx++] = TWDR;
                TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
                isr_state = I2C_IDLE;
            } else {
                isr_state = I2C_ERROR;
                TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
            }
            break;
            
        default:
            TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
            isr_state = I2C_IDLE;
            break;
    }
}
```

## Rust Implementation

Here's a modern Rust implementation using embedded-hal traits and interrupt-driven approach:

```rust
// i2c_interrupt.rs
#![no_std]

use core::cell::RefCell;
use cortex_m::interrupt::{free, Mutex};
use embedded_hal::i2c::{I2c, Operation};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cState {
    Idle,
    TxBusy,
    RxBusy,
    Complete,
    Error,
}

#[derive(Debug)]
pub enum I2cError {
    Busy,
    Nack,
    ArbitrationLost,
    BusError,
    Timeout,
}

pub struct I2cTransaction {
    pub device_addr: u8,
    pub tx_buffer: Option<&'static [u8]>,
    pub rx_buffer: Option<&'static mut [u8]>,
    pub tx_count: usize,
    pub rx_count: usize,
    pub state: I2cState,
}

impl I2cTransaction {
    pub const fn new() -> Self {
        Self {
            device_addr: 0,
            tx_buffer: None,
            rx_buffer: None,
            tx_count: 0,
            rx_count: 0,
            state: I2cState::Idle,
        }
    }
}

// Global transaction state protected by Mutex
static TRANSACTION: Mutex<RefCell<I2cTransaction>> = 
    Mutex::new(RefCell::new(I2cTransaction::new()));

// Callback function type
type Callback = fn(I2cState);
static mut CALLBACK: Option<Callback> = None;

pub struct InterruptI2c<I2C> {
    i2c: I2C,
}

impl<I2C> InterruptI2c<I2C>
where
    I2C: I2c,
{
    pub fn new(i2c: I2C) -> Self {
        Self { i2c }
    }

    pub fn write_async(
        &mut self,
        addr: u8,
        data: &'static [u8],
        callback: Callback,
    ) -> Result<(), I2cError> {
        free(|cs| {
            let mut trans = TRANSACTION.borrow(cs).borrow_mut();
            
            if trans.state != I2cState::Idle {
                return Err(I2cError::Busy);
            }

            trans.device_addr = addr;
            trans.tx_buffer = Some(data);
            trans.tx_count = 0;
            trans.rx_buffer = None;
            trans.state = I2cState::TxBusy;

            unsafe {
                CALLBACK = Some(callback);
            }

            // Enable I2C interrupts
            self.enable_interrupts();
            
            Ok(())
        })
    }

    pub fn read_async(
        &mut self,
        addr: u8,
        buffer: &'static mut [u8],
        callback: Callback,
    ) -> Result<(), I2cError> {
        free(|cs| {
            let mut trans = TRANSACTION.borrow(cs).borrow_mut();
            
            if trans.state != I2cState::Idle {
                return Err(I2cError::Busy);
            }

            trans.device_addr = addr;
            trans.tx_buffer = None;
            trans.rx_buffer = Some(buffer);
            trans.rx_count = 0;
            trans.state = I2cState::RxBusy;

            unsafe {
                CALLBACK = Some(callback);
            }

            self.enable_interrupts();
            
            Ok(())
        })
    }

    pub fn get_state(&self) -> I2cState {
        free(|cs| TRANSACTION.borrow(cs).borrow().state)
    }

    fn enable_interrupts(&self) {
        // Platform-specific interrupt enable
        // This would be implemented for your specific MCU
    }

    // Called from interrupt handler
    pub fn handle_interrupt(&mut self) {
        free(|cs| {
            let mut trans = TRANSACTION.borrow(cs).borrow_mut();
            
            match trans.state {
                I2cState::TxBusy => {
                    if let Some(tx_buf) = trans.tx_buffer {
                        if trans.tx_count < tx_buf.len() {
                            // Send next byte (platform-specific)
                            trans.tx_count += 1;
                        } else {
                            // Transfer complete
                            trans.state = I2cState::Complete;
                            self.complete_transaction(&mut trans);
                        }
                    }
                }
                I2cState::RxBusy => {
                    if let Some(rx_buf) = trans.rx_buffer.as_mut() {
                        if trans.rx_count < rx_buf.len() {
                            // Receive byte (platform-specific)
                            trans.rx_count += 1;
                        } else {
                            // Transfer complete
                            trans.state = I2cState::Complete;
                            self.complete_transaction(&mut trans);
                        }
                    }
                }
                _ => {}
            }
        });
    }

    fn complete_transaction(&self, trans: &mut I2cTransaction) {
        if let Some(callback) = unsafe { CALLBACK } {
            callback(trans.state);
        }
        trans.state = I2cState::Idle;
    }
}
```

### Rust Example using `embassy` Framework

Embassy provides excellent async/await support for embedded systems:

```rust
// Using Embassy framework for async I2C
#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use defmt::*;
use embassy_executor::Spawner;
use embassy_stm32::i2c::{self, I2c};
use embassy_stm32::time::Hertz;
use embassy_time::{Duration, Timer};
use {defmt_rtt as _, panic_probe as _};

// Sensor addresses
const MPU6050_ADDR: u8 = 0x68;
const BMP280_ADDR: u8 = 0x76;

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // Initialize I2C with interrupt support
    let i2c = I2c::new(
        p.I2C1,
        p.PB8,
        p.PB9,
        embassy_stm32::interrupt::take!(I2C1_EV),
        embassy_stm32::interrupt::take!(I2C1_ER),
        p.DMA1_CH6,
        p.DMA1_CH7,
        Hertz(100_000),
        Default::default(),
    );

    // Spawn concurrent I2C tasks
    spawner.spawn(read_accelerometer(i2c)).unwrap();
}

#[embassy_executor::task]
async fn read_accelerometer(mut i2c: I2c<'static>) {
    loop {
        let mut accel_data = [0u8; 6];
        
        // Write register address, then read data
        // This is non-blocking and uses interrupts under the hood
        match i2c.write_read(MPU6050_ADDR, &[0x3B], &mut accel_data).await {
            Ok(_) => {
                let x = i16::from_be_bytes([accel_data[0], accel_data[1]]);
                let y = i16::from_be_bytes([accel_data[2], accel_data[3]]);
                let z = i16::from_be_bytes([accel_data[4], accel_data[5]]);
                
                info!("Accel - X: {}, Y: {}, Z: {}", x, y, z);
            }
            Err(e) => error!("I2C error: {:?}", e),
        }
        
        // Other tasks can run during this delay
        Timer::after(Duration::from_millis(100)).await;
    }
}

// Example of reading from multiple sensors concurrently
#[embassy_executor::task]
async fn read_pressure(mut i2c: I2c<'static>) {
    loop {
        let mut temp_data = [0u8; 3];
        
        match i2c.write_read(BMP280_ADDR, &[0xFA], &mut temp_data).await {
            Ok(_) => {
                let raw = u32::from_be_bytes([0, temp_data[0], temp_data[1], temp_data[2]]);
                info!("Temperature raw: {}", raw >> 12);
            }
            Err(e) => error!("BMP280 error: {:?}", e),
        }
        
        Timer::after(Duration::from_millis(500)).await;
    }
}
```

### More Advanced Rust Example with Queue

```rust
use heapless::spsc::{Queue, Producer, Consumer};
use core::cell::RefCell;
use cortex_m::interrupt::{free, Mutex};

const QUEUE_SIZE: usize = 8;

pub enum I2cRequest {
    Write { addr: u8, data: [u8; 16], len: usize },
    Read { addr: u8, reg: u8, len: usize },
}

pub struct I2cResponse {
    pub addr: u8,
    pub data: [u8; 16],
    pub len: usize,
    pub success: bool,
}

static REQUEST_QUEUE: Mutex<RefCell<Option<Queue<I2cRequest, QUEUE_SIZE>>>> = 
    Mutex::new(RefCell::new(None));
static RESPONSE_QUEUE: Mutex<RefCell<Option<Queue<I2cResponse, QUEUE_SIZE>>>> = 
    Mutex::new(RefCell::new(None));

pub struct I2cManager {
    req_producer: Producer<'static, I2cRequest, QUEUE_SIZE>,
    resp_consumer: Consumer<'static, I2cResponse, QUEUE_SIZE>,
}

impl I2cManager {
    pub fn init() -> Self {
        let req_queue = Queue::new();
        let resp_queue = Queue::new();
        
        let (req_prod, req_cons) = req_queue.split();
        let (resp_prod, resp_cons) = resp_queue.split();
        
        free(|cs| {
            REQUEST_QUEUE.borrow(cs).replace(Some(req_queue));
            RESPONSE_QUEUE.borrow(cs).replace(Some(resp_queue));
        });
        
        Self {
            req_producer: req_prod,
            resp_consumer: resp_cons,
        }
    }
    
    pub fn write(&mut self, addr: u8, data: &[u8]) -> Result<(), ()> {
        let mut buf = [0u8; 16];
        let len = data.len().min(16);
        buf[..len].copy_from_slice(&data[..len]);
        
        self.req_producer.enqueue(I2cRequest::Write { addr, data: buf, len })
            .map_err(|_| ())
    }
    
    pub fn read(&mut self, addr: u8, reg: u8, len: usize) -> Result<(), ()> {
        self.req_producer.enqueue(I2cRequest::Read { addr, reg, len })
            .map_err(|_| ())
    }
    
    pub fn poll_response(&mut self) -> Option<I2cResponse> {
        self.resp_consumer.dequeue()
    }
}

// In interrupt handler, process requests from queue
pub fn process_i2c_queue()
{
    // Dequeue request, perform I2C operation, enqueue response
}
```

## Best Practices

1. **Keep ISRs short**: Do minimal work in the interrupt handler, just manage state transitions
2. **Use volatile variables**: Shared data between ISR and main code must be volatile
3. **Protect critical sections**: Use interrupt masking or mutexes when accessing shared state
4. **Handle errors gracefully**: Always check for NACK, arbitration loss, and bus errors
5. **Implement timeouts**: Don't let a failed transfer hang forever
6. **Use state machines**: Clear state tracking makes debugging much easier
7. **Test error conditions**: Deliberately trigger NACKs and bus errors during development

## Common Pitfalls

- Forgetting to re-enable interrupts after handling
- Not checking busy status before starting new transfer
- Buffer overruns when receiving unexpected data lengths
- Race conditions between ISR and main code
- Not handling all interrupt sources
- Assuming transfers always succeed

Interrupt-driven I2C is more complex than blocking I2C but essential for building responsive, efficient embedded systems. The examples above provide solid foundations you can adapt to your specific hardware platform.