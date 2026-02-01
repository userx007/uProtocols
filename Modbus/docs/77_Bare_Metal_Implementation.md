# Bare-Metal Modbus Implementation

## Overview

Bare-metal Modbus implementation refers to developing Modbus communication protocol stacks that run directly on microcontroller hardware without an underlying operating system (no Linux, FreeRTOS, etc.). This approach is common in resource-constrained embedded systems where you need maximum control over timing, minimal memory footprint, and deterministic behavior.

## Key Characteristics

**Direct Hardware Access**
- Registers and peripherals are accessed directly via memory-mapped I/O
- No OS abstraction layers or HAL dependencies (or minimal HAL)
- Complete control over interrupt handling and timing

**Resource Constraints**
- Limited RAM (often 2-64KB)
- Limited Flash/ROM (typically 16-512KB)
- No dynamic memory allocation (malloc/free avoided)
- Static buffer allocation

**Timing Control**
- Deterministic response times
- Direct control over UART timing and character gaps
- Precise implementation of Modbus RTU 3.5 character timeout

## Architecture Components

A bare-metal Modbus implementation typically includes:

1. **UART/Serial Driver** - Low-level communication
2. **Timer Management** - For inter-character and inter-frame timeouts
3. **State Machine** - Protocol handling
4. **CRC Calculation** - For Modbus RTU
5. **Register/Coil Storage** - Application data

---

## C/C++ Implementation Example

### UART Driver Layer

```c
// modbus_uart.h
#ifndef MODBUS_UART_H
#define MODBUS_UART_H

#include <stdint.h>
#include <stdbool.h>

// Hardware-specific register definitions (example for generic ARM Cortex-M)
#define UART_BASE_ADDR   0x40004000
#define UART_DR         (*(volatile uint32_t*)(UART_BASE_ADDR + 0x00))
#define UART_SR         (*(volatile uint32_t*)(UART_BASE_ADDR + 0x04))
#define UART_CR1        (*(volatile uint32_t*)(UART_BASE_ADDR + 0x0C))

#define UART_SR_RXNE    (1 << 5)  // RX Not Empty
#define UART_SR_TXE     (1 << 7)  // TX Empty

void uart_init(uint32_t baudrate);
bool uart_data_available(void);
uint8_t uart_read_byte(void);
void uart_write_byte(uint8_t data);

#endif
```

```c
// modbus_uart.c
#include "modbus_uart.h"

void uart_init(uint32_t baudrate) {
    // Simplified initialization
    // In reality, calculate BRR based on baudrate and clock
    UART_CR1 = 0;  // Disable UART
    
    // Configure baudrate (formula depends on MCU)
    // BRR = CLOCK_FREQ / baudrate;
    
    UART_CR1 |= (1 << 13);  // Enable UART
    UART_CR1 |= (1 << 2);   // Enable RX
    UART_CR1 |= (1 << 3);   // Enable TX
}

bool uart_data_available(void) {
    return (UART_SR & UART_SR_RXNE) != 0;
}

uint8_t uart_read_byte(void) {
    while (!uart_data_available());
    return (uint8_t)(UART_DR & 0xFF);
}

void uart_write_byte(uint8_t data) {
    while (!(UART_SR & UART_SR_TXE));
    UART_DR = data;
}
```

### Timer for Modbus RTU Timing

```c
// modbus_timer.h
#ifndef MODBUS_TIMER_H
#define MODBUS_TIMER_H

#include <stdint.h>
#include <stdbool.h>

void timer_init(uint32_t baudrate);
void timer_start(void);
void timer_stop(void);
bool timer_expired(void);
void timer_reset(void);

#endif
```

```c
// modbus_timer.c
#include "modbus_timer.h"

#define TIMER_BASE      0x40000000
#define TIM_CR1         (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIM_SR          (*(volatile uint32_t*)(TIMER_BASE + 0x10))
#define TIM_CNT         (*(volatile uint32_t*)(TIMER_BASE + 0x24))
#define TIM_ARR         (*(volatile uint32_t*)(TIMER_BASE + 0x2C))

static volatile bool timeout_flag = false;

void timer_init(uint32_t baudrate) {
    // Calculate 3.5 character time for Modbus RTU
    // At 9600 baud: 1 char = 11 bits = ~1.146ms
    // 3.5 chars = ~4ms
    
    uint32_t char_time_us = (11 * 1000000) / baudrate;
    uint32_t timeout_us = (char_time_us * 35) / 10;
    
    // Configure timer (simplified, assumes 1MHz timer clock)
    TIM_ARR = timeout_us;
    TIM_CR1 |= (1 << 2);  // Update interrupt enable
}

void timer_start(void) {
    timeout_flag = false;
    TIM_CNT = 0;
    TIM_CR1 |= (1 << 0);  // Enable timer
}

void timer_stop(void) {
    TIM_CR1 &= ~(1 << 0);  // Disable timer
}

bool timer_expired(void) {
    return timeout_flag;
}

void timer_reset(void) {
    TIM_CNT = 0;
    timeout_flag = false;
}

// Timer ISR (would be registered in vector table)
void TIM_IRQHandler(void) {
    if (TIM_SR & 0x01) {  // Update interrupt flag
        TIM_SR &= ~0x01;   // Clear flag
        timeout_flag = true;
        timer_stop();
    }
}
```

### Core Modbus RTU Implementation

```c
// modbus_rtu.h
#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>
#include <stdbool.h>

#define MODBUS_MAX_FRAME    256
#define MODBUS_SLAVE_ADDR   1

// Function codes
#define MODBUS_FC_READ_COILS            0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS  0x02
#define MODBUS_FC_READ_HOLDING_REGS     0x03
#define MODBUS_FC_READ_INPUT_REGS       0x04
#define MODBUS_FC_WRITE_SINGLE_COIL     0x05
#define MODBUS_FC_WRITE_SINGLE_REG      0x06

typedef enum {
    MODBUS_STATE_IDLE,
    MODBUS_STATE_RECEIVING,
    MODBUS_STATE_PROCESSING,
    MODBUS_STATE_RESPONDING
} modbus_state_t;

typedef struct {
    uint8_t buffer[MODBUS_MAX_FRAME];
    uint16_t length;
    modbus_state_t state;
} modbus_context_t;

void modbus_init(modbus_context_t *ctx, uint32_t baudrate);
void modbus_process(modbus_context_t *ctx);
uint16_t modbus_crc16(const uint8_t *data, uint16_t length);

#endif
```

```c
// modbus_rtu.c
#include "modbus_rtu.h"
#include "modbus_uart.h"
#include "modbus_timer.h"
#include <string.h>

// Application data storage
static uint16_t holding_registers[100];
static uint16_t input_registers[100];

void modbus_init(modbus_context_t *ctx, uint32_t baudrate) {
    uart_init(baudrate);
    timer_init(baudrate);
    
    ctx->length = 0;
    ctx->state = MODBUS_STATE_IDLE;
    
    memset(holding_registers, 0, sizeof(holding_registers));
    memset(input_registers, 0, sizeof(input_registers));
}

uint16_t modbus_crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
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

static void modbus_send_response(modbus_context_t *ctx) {
    // Send response frame
    for (uint16_t i = 0; i < ctx->length; i++) {
        uart_write_byte(ctx->buffer[i]);
    }
    ctx->state = MODBUS_STATE_IDLE;
    ctx->length = 0;
}

static void modbus_handle_read_holding_registers(modbus_context_t *ctx) {
    uint16_t start_addr = (ctx->buffer[2] << 8) | ctx->buffer[3];
    uint16_t num_regs = (ctx->buffer[4] << 8) | ctx->buffer[5];
    
    // Validate request
    if (start_addr + num_regs > 100) {
        // Send exception response
        ctx->buffer[1] |= 0x80;  // Set MSB of function code
        ctx->buffer[2] = 0x02;   // Illegal data address
        ctx->length = 3;
    } else {
        // Build response
        ctx->buffer[2] = num_regs * 2;  // Byte count
        
        for (uint16_t i = 0; i < num_regs; i++) {
            uint16_t reg_val = holding_registers[start_addr + i];
            ctx->buffer[3 + i * 2] = reg_val >> 8;
            ctx->buffer[4 + i * 2] = reg_val & 0xFF;
        }
        
        ctx->length = 3 + num_regs * 2;
    }
    
    // Add CRC
    uint16_t crc = modbus_crc16(ctx->buffer, ctx->length);
    ctx->buffer[ctx->length++] = crc & 0xFF;
    ctx->buffer[ctx->length++] = crc >> 8;
}

static void modbus_process_frame(modbus_context_t *ctx) {
    // Check minimum frame length (addr + func + crc = 4 bytes)
    if (ctx->length < 4) {
        ctx->state = MODBUS_STATE_IDLE;
        return;
    }
    
    // Verify CRC
    uint16_t received_crc = ctx->buffer[ctx->length - 2] | 
                           (ctx->buffer[ctx->length - 1] << 8);
    uint16_t calculated_crc = modbus_crc16(ctx->buffer, ctx->length - 2);
    
    if (received_crc != calculated_crc) {
        ctx->state = MODBUS_STATE_IDLE;
        return;
    }
    
    // Check if message is for this device
    if (ctx->buffer[0] != MODBUS_SLAVE_ADDR) {
        ctx->state = MODBUS_STATE_IDLE;
        return;
    }
    
    // Process function code
    switch (ctx->buffer[1]) {
        case MODBUS_FC_READ_HOLDING_REGS:
            modbus_handle_read_holding_registers(ctx);
            break;
            
        // Add other function codes here
        
        default:
            // Unsupported function code
            ctx->buffer[1] |= 0x80;
            ctx->buffer[2] = 0x01;  // Illegal function
            ctx->length = 3;
            uint16_t crc = modbus_crc16(ctx->buffer, 3);
            ctx->buffer[3] = crc & 0xFF;
            ctx->buffer[4] = crc >> 8;
            ctx->length = 5;
            break;
    }
    
    ctx->state = MODBUS_STATE_RESPONDING;
}

void modbus_process(modbus_context_t *ctx) {
    switch (ctx->state) {
        case MODBUS_STATE_IDLE:
            if (uart_data_available()) {
                ctx->buffer[0] = uart_read_byte();
                ctx->length = 1;
                ctx->state = MODBUS_STATE_RECEIVING;
                timer_start();
            }
            break;
            
        case MODBUS_STATE_RECEIVING:
            if (uart_data_available()) {
                if (ctx->length < MODBUS_MAX_FRAME) {
                    ctx->buffer[ctx->length++] = uart_read_byte();
                    timer_reset();
                    timer_start();
                }
            } else if (timer_expired()) {
                // Frame complete (3.5 char timeout)
                ctx->state = MODBUS_STATE_PROCESSING;
            }
            break;
            
        case MODBUS_STATE_PROCESSING:
            modbus_process_frame(ctx);
            break;
            
        case MODBUS_STATE_RESPONDING:
            modbus_send_response(ctx);
            break;
    }
}
```

### Main Application

```c
// main.c
#include "modbus_rtu.h"

int main(void) {
    modbus_context_t modbus_ctx;
    
    // Initialize system clock, etc.
    // system_init();
    
    // Initialize Modbus at 9600 baud
    modbus_init(&modbus_ctx, 9600);
    
    // Main loop
    while (1) {
        modbus_process(&modbus_ctx);
        
        // Application-specific code here
        // Update input_registers, process holding_registers, etc.
    }
    
    return 0;
}
```

---

## Rust Implementation Example

Rust is increasingly popular for bare-metal embedded development due to its safety guarantees and zero-cost abstractions.

### Hardware Abstraction

```rust
// uart.rs
use core::ptr::{read_volatile, write_volatile};

const UART_BASE: usize = 0x4000_4000;
const UART_DR: *mut u32 = (UART_BASE + 0x00) as *mut u32;
const UART_SR: *mut u32 = (UART_BASE + 0x04) as *mut u32;
const UART_CR1: *mut u32 = (UART_BASE + 0x0C) as *mut u32;

const UART_SR_RXNE: u32 = 1 << 5;
const UART_SR_TXE: u32 = 1 << 7;

pub struct Uart;

impl Uart {
    pub fn new(baudrate: u32) -> Self {
        unsafe {
            // Disable UART
            write_volatile(UART_CR1, 0);
            
            // Configure baudrate (simplified)
            // In practice: calculate BRR from clock frequency
            
            // Enable UART, RX, TX
            let mut cr1 = read_volatile(UART_CR1);
            cr1 |= (1 << 13) | (1 << 2) | (1 << 3);
            write_volatile(UART_CR1, cr1);
        }
        
        Uart
    }
    
    pub fn data_available(&self) -> bool {
        unsafe { (read_volatile(UART_SR) & UART_SR_RXNE) != 0 }
    }
    
    pub fn read_byte(&self) -> u8 {
        while !self.data_available() {}
        unsafe { (read_volatile(UART_DR) & 0xFF) as u8 }
    }
    
    pub fn write_byte(&self, data: u8) {
        unsafe {
            while (read_volatile(UART_SR) & UART_SR_TXE) == 0 {}
            write_volatile(UART_DR, data as u32);
        }
    }
}
```

### Timer Implementation

```rust
// timer.rs
use core::ptr::{read_volatile, write_volatile};
use core::sync::atomic::{AtomicBool, Ordering};

const TIMER_BASE: usize = 0x4000_0000;
const TIM_CR1: *mut u32 = (TIMER_BASE + 0x00) as *mut u32;
const TIM_SR: *mut u32 = (TIMER_BASE + 0x10) as *mut u32;
const TIM_CNT: *mut u32 = (TIMER_BASE + 0x24) as *mut u32;
const TIM_ARR: *mut u32 = (TIMER_BASE + 0x2C) as *mut u32;

static TIMEOUT_FLAG: AtomicBool = AtomicBool::new(false);

pub struct Timer;

impl Timer {
    pub fn new(baudrate: u32) -> Self {
        // Calculate 3.5 character time
        let char_time_us = (11 * 1_000_000) / baudrate;
        let timeout_us = (char_time_us * 35) / 10;
        
        unsafe {
            write_volatile(TIM_ARR, timeout_us);
            let mut cr1 = read_volatile(TIM_CR1);
            cr1 |= 1 << 2; // Update interrupt enable
            write_volatile(TIM_CR1, cr1);
        }
        
        Timer
    }
    
    pub fn start(&self) {
        TIMEOUT_FLAG.store(false, Ordering::Relaxed);
        unsafe {
            write_volatile(TIM_CNT, 0);
            let mut cr1 = read_volatile(TIM_CR1);
            cr1 |= 1; // Enable timer
            write_volatile(TIM_CR1, cr1);
        }
    }
    
    pub fn stop(&self) {
        unsafe {
            let mut cr1 = read_volatile(TIM_CR1);
            cr1 &= !1; // Disable timer
            write_volatile(TIM_CR1, cr1);
        }
    }
    
    pub fn expired(&self) -> bool {
        TIMEOUT_FLAG.load(Ordering::Relaxed)
    }
    
    pub fn reset(&self) {
        unsafe { write_volatile(TIM_CNT, 0); }
        TIMEOUT_FLAG.store(false, Ordering::Relaxed);
    }
}

// Interrupt handler
#[no_mangle]
pub extern "C" fn TIM_IRQHandler() {
    unsafe {
        let sr = read_volatile(TIM_SR);
        if (sr & 0x01) != 0 {
            write_volatile(TIM_SR, sr & !0x01); // Clear flag
            TIMEOUT_FLAG.store(true, Ordering::Relaxed);
        }
    }
}
```

### Modbus RTU Core

```rust
// modbus_rtu.rs
use crate::uart::Uart;
use crate::timer::Timer;

const MODBUS_MAX_FRAME: usize = 256;
const MODBUS_SLAVE_ADDR: u8 = 1;

// Function codes
const FC_READ_HOLDING_REGS: u8 = 0x03;
const FC_WRITE_SINGLE_REG: u8 = 0x06;

#[derive(Copy, Clone, PartialEq)]
enum ModbusState {
    Idle,
    Receiving,
    Processing,
    Responding,
}

pub struct ModbusContext {
    uart: Uart,
    timer: Timer,
    buffer: [u8; MODBUS_MAX_FRAME],
    length: usize,
    state: ModbusState,
    holding_registers: [u16; 100],
    input_registers: [u16; 100],
}

impl ModbusContext {
    pub fn new(baudrate: u32) -> Self {
        ModbusContext {
            uart: Uart::new(baudrate),
            timer: Timer::new(baudrate),
            buffer: [0; MODBUS_MAX_FRAME],
            length: 0,
            state: ModbusState::Idle,
            holding_registers: [0; 100],
            input_registers: [0; 100],
        }
    }
    
    fn crc16(data: &[u8]) -> u16 {
        let mut crc: u16 = 0xFFFF;
        
        for &byte in data {
            crc ^= byte as u16;
            for _ in 0..8 {
                if (crc & 0x0001) != 0 {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        
        crc
    }
    
    fn handle_read_holding_registers(&mut self) {
        let start_addr = ((self.buffer[2] as u16) << 8) | (self.buffer[3] as u16);
        let num_regs = ((self.buffer[4] as u16) << 8) | (self.buffer[5] as u16);
        
        if start_addr as usize + num_regs as usize > self.holding_registers.len() {
            // Exception response
            self.buffer[1] |= 0x80;
            self.buffer[2] = 0x02; // Illegal data address
            self.length = 3;
        } else {
            // Build response
            self.buffer[2] = (num_regs * 2) as u8;
            
            for i in 0..num_regs {
                let reg_val = self.holding_registers[(start_addr + i) as usize];
                self.buffer[3 + (i * 2) as usize] = (reg_val >> 8) as u8;
                self.buffer[4 + (i * 2) as usize] = (reg_val & 0xFF) as u8;
            }
            
            self.length = 3 + (num_regs * 2) as usize;
        }
        
        // Add CRC
        let crc = Self::crc16(&self.buffer[..self.length]);
        self.buffer[self.length] = (crc & 0xFF) as u8;
        self.buffer[self.length + 1] = (crc >> 8) as u8;
        self.length += 2;
    }
    
    fn process_frame(&mut self) {
        if self.length < 4 {
            self.state = ModbusState::Idle;
            return;
        }
        
        // Verify CRC
        let received_crc = (self.buffer[self.length - 2] as u16) |
                          ((self.buffer[self.length - 1] as u16) << 8);
        let calculated_crc = Self::crc16(&self.buffer[..self.length - 2]);
        
        if received_crc != calculated_crc {
            self.state = ModbusState::Idle;
            return;
        }
        
        // Check address
        if self.buffer[0] != MODBUS_SLAVE_ADDR {
            self.state = ModbusState::Idle;
            return;
        }
        
        // Process function code
        match self.buffer[1] {
            FC_READ_HOLDING_REGS => self.handle_read_holding_registers(),
            _ => {
                // Unsupported function
                self.buffer[1] |= 0x80;
                self.buffer[2] = 0x01; // Illegal function
                self.length = 3;
                let crc = Self::crc16(&self.buffer[..3]);
                self.buffer[3] = (crc & 0xFF) as u8;
                self.buffer[4] = (crc >> 8) as u8;
                self.length = 5;
            }
        }
        
        self.state = ModbusState::Responding;
    }
    
    fn send_response(&mut self) {
        for i in 0..self.length {
            self.uart.write_byte(self.buffer[i]);
        }
        self.state = ModbusState::Idle;
        self.length = 0;
    }
    
    pub fn process(&mut self) {
        match self.state {
            ModbusState::Idle => {
                if self.uart.data_available() {
                    self.buffer[0] = self.uart.read_byte();
                    self.length = 1;
                    self.state = ModbusState::Receiving;
                    self.timer.start();
                }
            }
            
            ModbusState::Receiving => {
                if self.uart.data_available() {
                    if self.length < MODBUS_MAX_FRAME {
                        self.buffer[self.length] = self.uart.read_byte();
                        self.length += 1;
                        self.timer.reset();
                        self.timer.start();
                    }
                } else if self.timer.expired() {
                    self.state = ModbusState::Processing;
                }
            }
            
            ModbusState::Processing => {
                self.process_frame();
            }
            
            ModbusState::Responding => {
                self.send_response();
            }
        }
    }
    
    // Public accessors for application
    pub fn set_holding_register(&mut self, addr: u16, value: u16) {
        if (addr as usize) < self.holding_registers.len() {
            self.holding_registers[addr as usize] = value;
        }
    }
    
    pub fn get_holding_register(&self, addr: u16) -> Option<u16> {
        self.holding_registers.get(addr as usize).copied()
    }
}
```

### Main Application (Rust)

```rust
// main.rs
#![no_std]
#![no_main]

mod uart;
mod timer;
mod modbus_rtu;

use modbus_rtu::ModbusContext;
use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn main() -> ! {
    // Initialize system (clocks, etc.)
    // system_init();
    
    let mut modbus = ModbusContext::new(9600);
    
    // Main loop
    loop {
        modbus.process();
        
        // Application logic
        // Example: update a register based on sensor reading
        // let sensor_value = read_sensor();
        // modbus.set_holding_register(0, sensor_value);
    }
}
```

---

## Summary

**Bare-metal Modbus implementation** involves creating a complete protocol stack that runs directly on microcontroller hardware without OS support. Key aspects include:

**Benefits:**
- **Deterministic timing** - Predictable response times
- **Minimal footprint** - Typically 5-20KB flash, 1-4KB RAM
- **Maximum control** - Direct hardware access and interrupt handling
- **No OS overhead** - Lower power consumption and faster execution

**Challenges:**
- **Manual resource management** - No dynamic memory, careful buffer sizing
- **Timing precision** - Implementing accurate 3.5 character timeout for RTU
- **Hardware dependencies** - Code tied to specific microcontroller peripherals
- **Limited debugging** - No printf, debugger support varies

**Critical Implementation Points:**
1. **State machine design** - Handle receiving, processing, responding states
2. **Timer accuracy** - 3.5 character timeout is essential for RTU frame detection
3. **Buffer management** - Static allocation, careful size limits
4. **CRC calculation** - Efficient implementation crucial for performance
5. **Interrupt handling** - UART and timer ISRs must be efficient

Bare-metal implementations are ideal for industrial sensors, actuators, and simple PLCs where reliability, cost, and power consumption are paramount. Both C/C++ and Rust are viable choices, with Rust offering memory safety guarantees that can prevent common embedded bugs.