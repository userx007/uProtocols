# Profibus Microcontroller Integration

## Overview

Integrating Profibus protocols into microcontroller systems enables industrial devices to communicate on Profibus networks. This involves implementing the physical layer (typically RS-485), data link layer (FDL - Fieldbus Data Link), and application protocols (DP or PA) on resource-constrained embedded platforms like STM32, ARM Cortex-M, or other industrial microcontrollers.

## Key Concepts

### Hardware Requirements
- **RS-485 Transceiver**: Physical layer interface (e.g., MAX485, SN75176)
- **UART/USART**: Serial communication peripheral with sufficient baud rate support
- **GPIO**: For direction control (TX/RX switching) on RS-485
- **Timers**: For precise timing and timeout management
- **Memory**: Sufficient RAM/Flash for protocol stack and application data

### Protocol Layers
1. **Physical Layer**: RS-485 differential signaling (9.6 kbps to 12 Mbps)
2. **Data Link Layer**: Token passing, frame handling, error detection
3. **Application Layer**: DP-V0/V1/V2 cyclic/acyclic data exchange

### Implementation Challenges
- Real-time requirements for token response times
- Precise timing for bus arbitration
- Interrupt-driven UART handling
- Memory-efficient buffer management
- Compliance with Profibus timing specifications

## C/C++ Implementation Example (STM32)

### Hardware Abstraction Layer

```c
// profibus_hal.h
#ifndef PROFIBUS_HAL_H
#define PROFIBUS_HAL_H

#include <stdint.h>
#include <stdbool.h>

// RS-485 direction control
typedef enum {
    RS485_RX_MODE = 0,
    RS485_TX_MODE = 1
} rs485_direction_t;

// Initialize UART and GPIO for Profibus
void profibus_hal_init(uint32_t baudrate);

// RS-485 direction control
void profibus_hal_set_direction(rs485_direction_t dir);

// Byte transmission
void profibus_hal_send_byte(uint8_t data);

// Byte reception (non-blocking)
bool profibus_hal_receive_byte(uint8_t *data);

// Timing functions
void profibus_hal_delay_us(uint32_t microseconds);
uint32_t profibus_hal_get_tick_us(void);

#endif
```

```c
// profibus_hal.c - STM32 HAL implementation
#include "profibus_hal.h"
#include "stm32f4xx_hal.h"

static UART_HandleTypeDef huart1;
static GPIO_TypeDef *de_port = GPIOA;
static uint16_t de_pin = GPIO_PIN_1;

void profibus_hal_init(uint32_t baudrate) {
    // Configure UART1 for Profibus
    huart1.Instance = USART1;
    huart1.Init.BaudRate = baudrate;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_EVEN; // Profibus uses even parity
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
    
    // Configure DE (Driver Enable) pin for RS-485
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = de_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(de_port, &GPIO_InitStruct);
    
    profibus_hal_set_direction(RS485_RX_MODE);
}

void profibus_hal_set_direction(rs485_direction_t dir) {
    HAL_GPIO_WritePin(de_port, de_pin, (GPIO_PinState)dir);
    // Small delay to allow transceiver to switch
    for (volatile int i = 0; i < 10; i++);
}

void profibus_hal_send_byte(uint8_t data) {
    HAL_UART_Transmit(&huart1, &data, 1, HAL_MAX_DELAY);
}

bool profibus_hal_receive_byte(uint8_t *data) {
    if (HAL_UART_Receive(&huart1, data, 1, 0) == HAL_OK) {
        return true;
    }
    return false;
}

void profibus_hal_delay_us(uint32_t microseconds) {
    uint32_t start = __HAL_TIM_GET_COUNTER(&htim2);
    while ((__HAL_TIM_GET_COUNTER(&htim2) - start) < microseconds);
}

uint32_t profibus_hal_get_tick_us(void) {
    return __HAL_TIM_GET_COUNTER(&htim2);
}
```

### Profibus Frame Handler

```c
// profibus_frame.h
#ifndef PROFIBUS_FRAME_H
#define PROFIBUS_FRAME_H

#include <stdint.h>
#include <stdbool.h>

#define PROFIBUS_SD1        0x10  // Start delimiter without data
#define PROFIBUS_SD2        0x68  // Start delimiter with data
#define PROFIBUS_SD3        0xA2  // Start delimiter for fixed length
#define PROFIBUS_SD4        0xDC  // Token frame
#define PROFIBUS_ED         0x16  // End delimiter
#define PROFIBUS_SC         0xE5  // Short acknowledgement

#define MAX_FRAME_LENGTH    256
#define MAX_DATA_LENGTH     246

typedef enum {
    FRAME_TYPE_TOKEN = 0,
    FRAME_TYPE_DATA_EXCHANGE,
    FRAME_TYPE_REQUEST,
    FRAME_TYPE_RESPONSE
} profibus_frame_type_t;

typedef struct {
    uint8_t start_delimiter;
    uint8_t length;
    uint8_t length_repeat;
    uint8_t da;  // Destination address
    uint8_t sa;  // Source address
    uint8_t fc;  // Function code
    uint8_t data[MAX_DATA_LENGTH];
    uint8_t fcs; // Frame check sequence
    uint8_t end_delimiter;
    uint16_t data_len;
} profibus_frame_t;

// Calculate FCS (Frame Check Sequence)
uint8_t profibus_calculate_fcs(const uint8_t *data, uint16_t len);

// Build a Profibus frame
uint16_t profibus_build_frame(uint8_t *buffer, const profibus_frame_t *frame);

// Parse received frame
bool profibus_parse_frame(const uint8_t *buffer, uint16_t len, 
                          profibus_frame_t *frame);

#endif
```

```c
// profibus_frame.c
#include "profibus_frame.h"
#include <string.h>

uint8_t profibus_calculate_fcs(const uint8_t *data, uint16_t len) {
    uint8_t fcs = 0;
    for (uint16_t i = 0; i < len; i++) {
        fcs += data[i];
    }
    return fcs;
}

uint16_t profibus_build_frame(uint8_t *buffer, const profibus_frame_t *frame) {
    uint16_t pos = 0;
    
    if (frame->data_len == 0) {
        // SD1 frame (no data)
        buffer[pos++] = PROFIBUS_SD1;
        buffer[pos++] = frame->da;
        buffer[pos++] = frame->sa;
        buffer[pos++] = frame->fc;
        
        uint8_t fcs_data[3] = {frame->da, frame->sa, frame->fc};
        buffer[pos++] = profibus_calculate_fcs(fcs_data, 3);
        buffer[pos++] = PROFIBUS_ED;
    } else {
        // SD2 frame (with data)
        buffer[pos++] = PROFIBUS_SD2;
        buffer[pos++] = frame->data_len + 3; // DA + SA + FC
        buffer[pos++] = frame->data_len + 3; // Repeated length
        buffer[pos++] = PROFIBUS_SD2;
        buffer[pos++] = frame->da;
        buffer[pos++] = frame->sa;
        buffer[pos++] = frame->fc;
        
        memcpy(&buffer[pos], frame->data, frame->data_len);
        pos += frame->data_len;
        
        uint8_t fcs_len = frame->data_len + 3;
        uint8_t fcs_data[MAX_DATA_LENGTH + 3];
        fcs_data[0] = frame->da;
        fcs_data[1] = frame->sa;
        fcs_data[2] = frame->fc;
        memcpy(&fcs_data[3], frame->data, frame->data_len);
        
        buffer[pos++] = profibus_calculate_fcs(fcs_data, fcs_len);
        buffer[pos++] = PROFIBUS_ED;
    }
    
    return pos;
}

bool profibus_parse_frame(const uint8_t *buffer, uint16_t len,
                         profibus_frame_t *frame) {
    if (len < 6) return false;
    
    uint16_t pos = 0;
    frame->start_delimiter = buffer[pos++];
    
    if (frame->start_delimiter == PROFIBUS_SD1) {
        frame->da = buffer[pos++];
        frame->sa = buffer[pos++];
        frame->fc = buffer[pos++];
        frame->fcs = buffer[pos++];
        frame->end_delimiter = buffer[pos++];
        frame->data_len = 0;
        
        uint8_t fcs_data[3] = {frame->da, frame->sa, frame->fc};
        uint8_t calc_fcs = profibus_calculate_fcs(fcs_data, 3);
        
        return (frame->fcs == calc_fcs && 
                frame->end_delimiter == PROFIBUS_ED);
    } 
    else if (frame->start_delimiter == PROFIBUS_SD2) {
        frame->length = buffer[pos++];
        frame->length_repeat = buffer[pos++];
        
        if (frame->length != frame->length_repeat) return false;
        if (buffer[pos++] != PROFIBUS_SD2) return false;
        
        frame->da = buffer[pos++];
        frame->sa = buffer[pos++];
        frame->fc = buffer[pos++];
        frame->data_len = frame->length - 3;
        
        if (frame->data_len > MAX_DATA_LENGTH) return false;
        
        memcpy(frame->data, &buffer[pos], frame->data_len);
        pos += frame->data_len;
        
        frame->fcs = buffer[pos++];
        frame->end_delimiter = buffer[pos++];
        
        uint8_t fcs_data[MAX_DATA_LENGTH + 3];
        fcs_data[0] = frame->da;
        fcs_data[1] = frame->sa;
        fcs_data[2] = frame->fc;
        memcpy(&fcs_data[3], frame->data, frame->data_len);
        
        uint8_t calc_fcs = profibus_calculate_fcs(fcs_data, 
                                                   frame->length);
        
        return (frame->fcs == calc_fcs && 
                frame->end_delimiter == PROFIBUS_ED);
    }
    
    return false;
}
```

### Profibus DP Slave Implementation

```cpp
// profibus_slave.cpp
#include "profibus_hal.h"
#include "profibus_frame.h"
#include <cstring>

class ProfibusDP_Slave {
private:
    uint8_t station_address;
    uint8_t input_data[32];
    uint8_t output_data[32];
    uint8_t input_len;
    uint8_t output_len;
    uint8_t rx_buffer[MAX_FRAME_LENGTH];
    uint16_t rx_pos;
    
public:
    ProfibusDP_Slave(uint8_t address, uint8_t in_len, uint8_t out_len)
        : station_address(address), input_len(in_len), 
          output_len(out_len), rx_pos(0) {
        memset(input_data, 0, sizeof(input_data));
        memset(output_data, 0, sizeof(output_data));
        memset(rx_buffer, 0, sizeof(rx_buffer));
    }
    
    void init(uint32_t baudrate) {
        profibus_hal_init(baudrate);
        profibus_hal_set_direction(RS485_RX_MODE);
    }
    
    void process() {
        uint8_t byte;
        
        // Receive data
        while (profibus_hal_receive_byte(&byte)) {
            rx_buffer[rx_pos++] = byte;
            
            // Check for end delimiter
            if (byte == PROFIBUS_ED && rx_pos >= 6) {
                handle_frame();
                rx_pos = 0;
            }
            
            // Prevent buffer overflow
            if (rx_pos >= MAX_FRAME_LENGTH) {
                rx_pos = 0;
            }
        }
    }
    
    void set_input_data(const uint8_t *data, uint8_t len) {
        if (len <= input_len) {
            memcpy(input_data, data, len);
        }
    }
    
    void get_output_data(uint8_t *data, uint8_t len) {
        if (len <= output_len) {
            memcpy(data, output_data, len);
        }
    }
    
private:
    void handle_frame() {
        profibus_frame_t frame;
        
        if (!profibus_parse_frame(rx_buffer, rx_pos, &frame)) {
            return; // Invalid frame
        }
        
        // Check if frame is for this station
        if (frame.da != station_address) {
            return;
        }
        
        // Handle different function codes
        switch (frame.fc & 0x0F) {
            case 0x03: // Request FDL Status
                send_status_response();
                break;
                
            case 0x06: // Data Exchange (cyclic data)
                handle_data_exchange(&frame);
                break;
                
            case 0x0D: // Get Diagnosis
                send_diagnosis();
                break;
                
            default:
                // Unknown function code
                break;
        }
    }
    
    void send_status_response() {
        profibus_hal_set_direction(RS485_TX_MODE);
        profibus_hal_send_byte(PROFIBUS_SC); // Short acknowledgement
        profibus_hal_set_direction(RS485_RX_MODE);
    }
    
    void handle_data_exchange(const profibus_frame_t *request) {
        // Copy output data from master
        if (request->data_len <= output_len) {
            memcpy(output_data, request->data, request->data_len);
        }
        
        // Prepare response with input data
        profibus_frame_t response;
        response.da = request->sa; // Swap addresses
        response.sa = station_address;
        response.fc = request->fc | 0x08; // Response flag
        response.data_len = input_len;
        memcpy(response.data, input_data, input_len);
        
        uint8_t tx_buffer[MAX_FRAME_LENGTH];
        uint16_t tx_len = profibus_build_frame(tx_buffer, &response);
        
        // Send response
        profibus_hal_set_direction(RS485_TX_MODE);
        for (uint16_t i = 0; i < tx_len; i++) {
            profibus_hal_send_byte(tx_buffer[i]);
        }
        profibus_hal_set_direction(RS485_RX_MODE);
    }
    
    void send_diagnosis() {
        profibus_frame_t response;
        response.da = 0; // Master address (typically 0)
        response.sa = station_address;
        response.fc = 0x08; // Diagnosis response
        
        // Simple diagnosis: Station OK, no errors
        uint8_t diag_data[6] = {
            station_address,  // Station status
            0x00,             // Status 1
            0x00,             // Status 2
            0x00,             // Status 3
            0x00,             // Master address
            0x00              // Ident number high
        };
        
        response.data_len = 6;
        memcpy(response.data, diag_data, 6);
        
        uint8_t tx_buffer[MAX_FRAME_LENGTH];
        uint16_t tx_len = profibus_build_frame(tx_buffer, &response);
        
        profibus_hal_set_direction(RS485_TX_MODE);
        for (uint16_t i = 0; i < tx_len; i++) {
            profibus_hal_send_byte(tx_buffer[i]);
        }
        profibus_hal_set_direction(RS485_RX_MODE);
    }
};
```

## Rust Implementation Example

```rust
// profibus_hal.rs
#![no_std]

pub trait ProfibusHal {
    fn init(&mut self, baudrate: u32);
    fn set_direction(&mut self, tx_mode: bool);
    fn send_byte(&mut self, data: u8);
    fn receive_byte(&mut self) -> Option<u8>;
    fn delay_us(&mut self, microseconds: u32);
    fn get_tick_us(&self) -> u32;
}

// Example STM32 HAL implementation using embedded-hal traits
use embedded_hal::serial::{Read, Write};
use embedded_hal::digital::v2::OutputPin;

pub struct Stm32ProfibusHal<UART, PIN> {
    uart: UART,
    de_pin: PIN,
}

impl<UART, PIN, E> Stm32ProfibusHal<UART, PIN>
where
    UART: Read<u8, Error = E> + Write<u8, Error = E>,
    PIN: OutputPin,
{
    pub fn new(uart: UART, de_pin: PIN) -> Self {
        Self { uart, de_pin }
    }
}

impl<UART, PIN, E> ProfibusHal for Stm32ProfibusHal<UART, PIN>
where
    UART: Read<u8, Error = E> + Write<u8, Error = E>,
    PIN: OutputPin,
{
    fn init(&mut self, _baudrate: u32) {
        // Initialization handled by HAL setup
        self.set_direction(false); // RX mode
    }
    
    fn set_direction(&mut self, tx_mode: bool) {
        if tx_mode {
            let _ = self.de_pin.set_high();
        } else {
            let _ = self.de_pin.set_low();
        }
    }
    
    fn send_byte(&mut self, data: u8) {
        let _ = nb::block!(self.uart.write(data));
    }
    
    fn receive_byte(&mut self) -> Option<u8> {
        match self.uart.read() {
            Ok(byte) => Some(byte),
            Err(_) => None,
        }
    }
    
    fn delay_us(&mut self, microseconds: u32) {
        // Implementation depends on timer/delay provider
        cortex_m::asm::delay(microseconds * 72); // Assuming 72MHz
    }
    
    fn get_tick_us(&self) -> u32 {
        // Implementation depends on timer
        0 // Placeholder
    }
}
```

```rust
// profibus_frame.rs
#![no_std]

use heapless::Vec;

pub const SD1: u8 = 0x10;
pub const SD2: u8 = 0x68;
pub const SD3: u8 = 0xA2;
pub const SD4: u8 = 0xDC;
pub const ED: u8 = 0x16;
pub const SC: u8 = 0xE5;

pub const MAX_DATA_LEN: usize = 246;

#[derive(Debug, Clone)]
pub struct ProfibusFrame {
    pub start_delimiter: u8,
    pub da: u8,  // Destination address
    pub sa: u8,  // Source address
    pub fc: u8,  // Function code
    pub data: Vec<u8, MAX_DATA_LEN>,
}

impl ProfibusFrame {
    pub fn new(da: u8, sa: u8, fc: u8) -> Self {
        Self {
            start_delimiter: SD1,
            da,
            sa,
            fc,
            data: Vec::new(),
        }
    }
    
    pub fn calculate_fcs(data: &[u8]) -> u8 {
        data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b))
    }
    
    pub fn build(&self, buffer: &mut [u8]) -> Result<usize, ()> {
        let mut pos = 0;
        
        if self.data.is_empty() {
            // SD1 frame
            if buffer.len() < 6 {
                return Err(());
            }
            
            buffer[pos] = SD1;
            pos += 1;
            buffer[pos] = self.da;
            pos += 1;
            buffer[pos] = self.sa;
            pos += 1;
            buffer[pos] = self.fc;
            pos += 1;
            
            let fcs_data = [self.da, self.sa, self.fc];
            buffer[pos] = Self::calculate_fcs(&fcs_data);
            pos += 1;
            buffer[pos] = ED;
            pos += 1;
            
            Ok(pos)
        } else {
            // SD2 frame
            let len = self.data.len() + 3;
            if buffer.len() < len + 6 {
                return Err(());
            }
            
            buffer[pos] = SD2;
            pos += 1;
            buffer[pos] = len as u8;
            pos += 1;
            buffer[pos] = len as u8;
            pos += 1;
            buffer[pos] = SD2;
            pos += 1;
            buffer[pos] = self.da;
            pos += 1;
            buffer[pos] = self.sa;
            pos += 1;
            buffer[pos] = self.fc;
            pos += 1;
            
            buffer[pos..pos + self.data.len()]
                .copy_from_slice(&self.data);
            pos += self.data.len();
            
            let mut fcs_data = Vec::<u8, 249>::new();
            let _ = fcs_data.push(self.da);
            let _ = fcs_data.push(self.sa);
            let _ = fcs_data.push(self.fc);
            let _ = fcs_data.extend_from_slice(&self.data);
            
            buffer[pos] = Self::calculate_fcs(&fcs_data);
            pos += 1;
            buffer[pos] = ED;
            pos += 1;
            
            Ok(pos)
        }
    }
    
    pub fn parse(buffer: &[u8]) -> Result<Self, ()> {
        if buffer.len() < 6 {
            return Err(());
        }
        
        let mut pos = 0;
        let start_delimiter = buffer[pos];
        pos += 1;
        
        match start_delimiter {
            SD1 => {
                let da = buffer[pos];
                pos += 1;
                let sa = buffer[pos];
                pos += 1;
                let fc = buffer[pos];
                pos += 1;
                let fcs = buffer[pos];
                pos += 1;
                let ed = buffer[pos];
                
                if ed != ED {
                    return Err(());
                }
                
                let fcs_data = [da, sa, fc];
                let calc_fcs = Self::calculate_fcs(&fcs_data);
                
                if fcs != calc_fcs {
                    return Err(());
                }
                
                Ok(Self {
                    start_delimiter,
                    da,
                    sa,
                    fc,
                    data: Vec::new(),
                })
            }
            SD2 => {
                let len = buffer[pos];
                pos += 1;
                let len_repeat = buffer[pos];
                pos += 1;
                
                if len != len_repeat || buffer[pos] != SD2 {
                    return Err(());
                }
                pos += 1;
                
                let da = buffer[pos];
                pos += 1;
                let sa = buffer[pos];
                pos += 1;
                let fc = buffer[pos];
                pos += 1;
                
                let data_len = (len - 3) as usize;
                if data_len > MAX_DATA_LEN {
                    return Err(());
                }
                
                let mut data = Vec::new();
                for _ in 0..data_len {
                    let _ = data.push(buffer[pos]);
                    pos += 1;
                }
                
                let fcs = buffer[pos];
                pos += 1;
                let ed = buffer[pos];
                
                if ed != ED {
                    return Err(());
                }
                
                let mut fcs_data = Vec::<u8, 249>::new();
                let _ = fcs_data.push(da);
                let _ = fcs_data.push(sa);
                let _ = fcs_data.push(fc);
                let _ = fcs_data.extend_from_slice(&data);
                
                let calc_fcs = Self::calculate_fcs(&fcs_data);
                
                if fcs != calc_fcs {
                    return Err(());
                }
                
                Ok(Self {
                    start_delimiter,
                    da,
                    sa,
                    fc,
                    data,
                })
            }
            _ => Err(()),
        }
    }
}
```

```rust
// profibus_slave.rs
#![no_std]

use heapless::Vec;
use crate::profibus_frame::{ProfibusFrame, SC, ED, MAX_DATA_LEN};
use crate::profibus_hal::ProfibusHal;

pub struct ProfibusSlaveDP<HAL: ProfibusHal> {
    hal: HAL,
    station_address: u8,
    input_data: Vec<u8, 32>,
    output_data: Vec<u8, 32>,
    rx_buffer: Vec<u8, 256>,
}

impl<HAL: ProfibusHal> ProfibusSlaveDP<HAL> {
    pub fn new(hal: HAL, address: u8) -> Self {
        Self {
            hal,
            station_address: address,
            input_data: Vec::new(),
            output_data: Vec::new(),
            rx_buffer: Vec::new(),
        }
    }
    
    pub fn init(&mut self, baudrate: u32) {
        self.hal.init(baudrate);
        self.hal.set_direction(false); // RX mode
    }
    
    pub fn process(&mut self) {
        // Receive bytes
        while let Some(byte) = self.hal.receive_byte() {
            let _ = self.rx_buffer.push(byte);
            
            // Check for end delimiter
            if byte == ED && self.rx_buffer.len() >= 6 {
                self.handle_frame();
                self.rx_buffer.clear();
            }
            
            // Prevent overflow
            if self.rx_buffer.len() >= 256 {
                self.rx_buffer.clear();
            }
        }
    }
    
    pub fn set_input_data(&mut self, data: &[u8]) {
        self.input_data.clear();
        let _ = self.input_data.extend_from_slice(data);
    }
    
    pub fn get_output_data(&self) -> &[u8] {
        &self.output_data
    }
    
    fn handle_frame(&mut self) {
        let frame = match ProfibusFrame::parse(&self.rx_buffer) {
            Ok(f) => f,
            Err(_) => return,
        };
        
        // Check if frame is for this station
        if frame.da != self.station_address {
            return;
        }
        
        // Handle function codes
        match frame.fc & 0x0F {
            0x03 => self.send_status_response(),
            0x06 => self.handle_data_exchange(&frame),
            0x0D => self.send_diagnosis(),
            _ => {} // Unknown
        }
    }
    
    fn send_status_response(&mut self) {
        self.hal.set_direction(true);
        self.hal.send_byte(SC);
        self.hal.set_direction(false);
    }
    
    fn handle_data_exchange(&mut self, request: &ProfibusFrame) {
        // Copy output data from master
        self.output_data.clear();
        let _ = self.output_data.extend_from_slice(&request.data);
        
        // Prepare response
        let mut response = ProfibusFrame::new(
            request.sa,
            self.station_address,
            request.fc | 0x08,
        );
        let _ = response.data.extend_from_slice(&self.input_data);
        
        let mut tx_buffer = [0u8; 256];
        if let Ok(len) = response.build(&mut tx_buffer) {
            self.hal.set_direction(true);
            for i in 0..len {
                self.hal.send_byte(tx_buffer[i]);
            }
            self.hal.set_direction(false);
        }
    }
    
    fn send_diagnosis(&mut self) {
        let mut response = ProfibusFrame::new(
            0,
            self.station_address,
            0x08,
        );
        
        // Simple diagnosis data
        let diag = [self.station_address, 0x00, 0x00, 0x00, 0x00, 0x00];
        let _ = response.data.extend_from_slice(&diag);
        
        let mut tx_buffer = [0u8; 256];
        if let Ok(len) = response.build(&mut tx_buffer) {
            self.hal.set_direction(true);
            for i in 0..len {
                self.hal.send_byte(tx_buffer[i]);
            }
            self.hal.set_direction(false);
        }
    }
}
```

## Summary

**Profibus microcontroller integration** enables embedded devices to participate in industrial automation networks. Key implementation aspects include:

- **Hardware Layer**: RS-485 transceivers with direction control, UART configuration for even parity and appropriate baud rates (9.6 kbps - 12 Mbps)
- **Protocol Stack**: Frame construction/parsing with proper delimiters (SD1, SD2, ED), FCS calculation, and address filtering
- **Real-time Constraints**: Interrupt-driven reception, precise timing for token passing, and deterministic response times
- **Memory Efficiency**: Circular buffers, static allocation for embedded systems, and optimized data structures

The C/C++ examples demonstrate STM32 HAL integration with hardware abstraction layers, while the Rust implementation showcases embedded-hal traits and `no_std` patterns suitable for resource-constrained environments. Both approaches emphasize robust frame handling, error detection through FCS validation, and proper RS-485 transceiver control for half-duplex communication.

Successful integration requires careful attention to timing specifications, proper interrupt handling, and thorough testing with Profibus diagnostic tools to ensure compliance with the protocol standards.