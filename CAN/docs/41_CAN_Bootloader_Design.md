# CAN Bootloader Design: Comprehensive Guide

## Overview

A CAN bootloader is a specialized firmware component that enables over-the-air (OTA) firmware updates for embedded systems using the Controller Area Network (CAN) bus. This is critical in automotive, industrial, and IoT applications where physical access for updates is impractical or impossible.

## Core Concepts

### What is a CAN Bootloader?

A bootloader is a small program that runs before the main application firmware. In CAN-based systems, it:

- Listens for update commands on the CAN bus
- Receives new firmware data in chunks
- Validates firmware integrity (checksums, signatures)
- Writes new firmware to flash memory
- Boots into the updated application

### Memory Layout

```
┌─────────────────────┐ 0x08000000 (Flash Start)
│   Bootloader        │ (16-32 KB)
│   - CAN driver      │
│   - Flash routines  │
│   - Update logic    │
├─────────────────────┤ 0x08008000
│   Application       │
│   - Main firmware   │
│   - User code       │
│                     │
└─────────────────────┘ (Flash End)
```

## Bootloader Protocol Design

### Common Protocol Elements

1. **Session Management**: Handshake, authentication
2. **Data Transfer**: Chunked firmware transmission
3. **Verification**: CRC/checksum validation
4. **Flash Programming**: Erase and write operations
5. **Application Jump**: Transfer control to new firmware

### Protocol State Machine

```
[IDLE] → [AUTH] → [ERASE] → [PROGRAM] → [VERIFY] → [BOOT]
   ↓        ↓         ↓          ↓           ↓         ↓
[ERROR]←──────────────────────────────────────────────┘
```

## Implementation Examples

### 1. C/C++ Implementation (STM32-style)

#### Bootloader Main Loop

```c
// bootloader_main.c
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

#define APP_START_ADDRESS    0x08008000
#define BOOTLOADER_VERSION   0x0100
#define CAN_BOOTLOADER_ID    0x100
#define CAN_HOST_ID          0x200

// Command definitions
#define CMD_START_SESSION    0x01
#define CMD_ERASE_FLASH      0x02
#define CMD_WRITE_DATA       0x03
#define CMD_VERIFY           0x04
#define CMD_BOOT_APP         0x05
#define CMD_GET_VERSION      0x06

// Response codes
#define RESP_ACK             0x00
#define RESP_NACK            0x01
#define RESP_BUSY            0x02

typedef enum {
    STATE_IDLE,
    STATE_SESSION_ACTIVE,
    STATE_ERASING,
    STATE_PROGRAMMING,
    STATE_VERIFYING
} BootloaderState;

typedef struct {
    uint32_t base_address;
    uint32_t total_size;
    uint32_t bytes_received;
    uint32_t expected_crc;
    BootloaderState state;
    uint32_t timeout_counter;
} BootloaderContext;

static BootloaderContext ctx = {0};
static CAN_HandleTypeDef hcan1;

// Flash operations
bool flash_erase_app_area(uint32_t size) {
    HAL_FLASH_Unlock();
    
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;
    
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_init.Sector = FLASH_SECTOR_2; // Adjust based on memory map
    erase_init.NbSectors = 6; // Calculate based on size
    
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    HAL_FLASH_Lock();
    
    return (status == HAL_OK);
}

bool flash_write_data(uint32_t address, uint8_t *data, uint32_t length) {
    HAL_FLASH_Unlock();
    
    bool success = true;
    for (uint32_t i = 0; i < length; i += 4) {
        uint32_t word = *(uint32_t*)(data + i);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word) != HAL_OK) {
            success = false;
            break;
        }
    }
    
    HAL_FLASH_Lock();
    return success;
}

uint32_t calculate_crc32(uint32_t address, uint32_t length) {
    // Use hardware CRC peripheral if available
    CRC_HandleTypeDef hcrc;
    __HAL_RCC_CRC_CLK_ENABLE();
    hcrc.Instance = CRC;
    HAL_CRC_Init(&hcrc);
    
    uint32_t crc = HAL_CRC_Calculate(&hcrc, (uint32_t*)address, length / 4);
    return crc;
}

void jump_to_application(void) {
    // Check if valid application exists
    uint32_t app_stack = *(volatile uint32_t*)APP_START_ADDRESS;
    uint32_t app_entry = *(volatile uint32_t*)(APP_START_ADDRESS + 4);
    
    if ((app_stack & 0xFFF00000) != 0x20000000) {
        // Invalid stack pointer
        return;
    }
    
    // Disable interrupts
    __disable_irq();
    
    // Deinitialize peripherals
    HAL_CAN_DeInit(&hcan1);
    HAL_DeInit();
    
    // Set vector table offset
    SCB->VTOR = APP_START_ADDRESS;
    
    // Set stack pointer
    __set_MSP(app_stack);
    
    // Jump to application
    void (*app_reset_handler)(void) = (void*)app_entry;
    app_reset_handler();
}

void send_can_response(uint8_t cmd, uint8_t status, uint8_t *data, uint8_t len) {
    CAN_TxHeaderTypeDef tx_header;
    uint8_t tx_data[8];
    uint32_t tx_mailbox;
    
    tx_header.StdId = CAN_BOOTLOADER_ID;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = len + 2;
    
    tx_data[0] = cmd;
    tx_data[1] = status;
    
    if (data && len > 0) {
        for (int i = 0; i < len && i < 6; i++) {
            tx_data[i + 2] = data[i];
        }
    }
    
    HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &tx_mailbox);
}

void process_can_message(CAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data) {
    if (rx_header->StdId != CAN_HOST_ID) {
        return;
    }
    
    uint8_t cmd = rx_data[0];
    
    switch (cmd) {
        case CMD_START_SESSION: {
            ctx.state = STATE_SESSION_ACTIVE;
            ctx.total_size = *(uint32_t*)(&rx_data[1]);
            ctx.bytes_received = 0;
            ctx.expected_crc = *(uint32_t*)(&rx_data[5]);
            ctx.base_address = APP_START_ADDRESS;
            ctx.timeout_counter = 0;
            
            send_can_response(CMD_START_SESSION, RESP_ACK, NULL, 0);
            break;
        }
        
        case CMD_ERASE_FLASH: {
            if (ctx.state != STATE_SESSION_ACTIVE) {
                send_can_response(CMD_ERASE_FLASH, RESP_NACK, NULL, 0);
                break;
            }
            
            ctx.state = STATE_ERASING;
            send_can_response(CMD_ERASE_FLASH, RESP_BUSY, NULL, 0);
            
            if (flash_erase_app_area(ctx.total_size)) {
                ctx.state = STATE_PROGRAMMING;
                send_can_response(CMD_ERASE_FLASH, RESP_ACK, NULL, 0);
            } else {
                ctx.state = STATE_IDLE;
                send_can_response(CMD_ERASE_FLASH, RESP_NACK, NULL, 0);
            }
            break;
        }
        
        case CMD_WRITE_DATA: {
            if (ctx.state != STATE_PROGRAMMING) {
                send_can_response(CMD_WRITE_DATA, RESP_NACK, NULL, 0);
                break;
            }
            
            uint16_t offset = *(uint16_t*)(&rx_data[1]);
            uint8_t length = rx_data[3];
            uint8_t *data = &rx_data[4];
            
            uint32_t write_addr = ctx.base_address + offset;
            
            if (flash_write_data(write_addr, data, length)) {
                ctx.bytes_received += length;
                ctx.timeout_counter = 0;
                send_can_response(CMD_WRITE_DATA, RESP_ACK, NULL, 0);
            } else {
                send_can_response(CMD_WRITE_DATA, RESP_NACK, NULL, 0);
            }
            break;
        }
        
        case CMD_VERIFY: {
            ctx.state = STATE_VERIFYING;
            
            uint32_t calculated_crc = calculate_crc32(ctx.base_address, ctx.total_size);
            
            if (calculated_crc == ctx.expected_crc) {
                send_can_response(CMD_VERIFY, RESP_ACK, (uint8_t*)&calculated_crc, 4);
            } else {
                send_can_response(CMD_VERIFY, RESP_NACK, (uint8_t*)&calculated_crc, 4);
            }
            break;
        }
        
        case CMD_BOOT_APP: {
            send_can_response(CMD_BOOT_APP, RESP_ACK, NULL, 0);
            HAL_Delay(100); // Allow message to be sent
            jump_to_application();
            break;
        }
        
        case CMD_GET_VERSION: {
            uint16_t version = BOOTLOADER_VERSION;
            send_can_response(CMD_GET_VERSION, RESP_ACK, (uint8_t*)&version, 2);
            break;
        }
        
        default:
            send_can_response(cmd, RESP_NACK, NULL, 0);
            break;
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    
    // Initialize CAN
    hcan1.Instance = CAN1;
    hcan1.Init.Prescaler = 6;
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
    hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan1.Init.TimeTriggeredMode = DISABLE;
    hcan1.Init.AutoBusOff = ENABLE;
    hcan1.Init.AutoWakeUp = DISABLE;
    hcan1.Init.AutoRetransmission = ENABLE;
    hcan1.Init.ReceiveFifoLocked = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;
    
    HAL_CAN_Init(&hcan1);
    
    // Configure CAN filter
    CAN_FilterTypeDef filter;
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = CAN_HOST_ID << 5;
    filter.FilterIdLow = 0;
    filter.FilterMaskIdHigh = 0x7FF << 5;
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    
    HAL_CAN_ConfigFilter(&hcan1, &filter);
    HAL_CAN_Start(&hcan1);
    
    // Bootloader timeout: 5 seconds
    uint32_t bootloader_timeout = 5000;
    uint32_t start_time = HAL_GetTick();
    
    ctx.state = STATE_IDLE;
    
    while (1) {
        CAN_RxHeaderTypeDef rx_header;
        uint8_t rx_data[8];
        
        if (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) > 0) {
            if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK) {
                process_can_message(&rx_header, rx_data);
                start_time = HAL_GetTick(); // Reset timeout on activity
            }
        }
        
        // Timeout check - boot application if no activity
        if (ctx.state == STATE_IDLE && (HAL_GetTick() - start_time) > bootloader_timeout) {
            jump_to_application();
        }
        
        HAL_Delay(1);
    }
}
```

### 2. Rust Implementation (embedded-hal)

```rust
// bootloader.rs
#![no_std]
#![no_main]

use core::ptr;
use cortex_m_rt::entry;
use embedded_hal::can::{Frame, Id};
use stm32f4xx_hal::{
    can::Can,
    flash::{FlashExt, LockedFlash, UnlockedFlash},
    pac,
    prelude::*,
};

const APP_START_ADDRESS: u32 = 0x0800_8000;
const BOOTLOADER_VERSION: u16 = 0x0100;
const CAN_BOOTLOADER_ID: u16 = 0x100;
const CAN_HOST_ID: u16 = 0x200;

// Command definitions
const CMD_START_SESSION: u8 = 0x01;
const CMD_ERASE_FLASH: u8 = 0x02;
const CMD_WRITE_DATA: u8 = 0x03;
const CMD_VERIFY: u8 = 0x04;
const CMD_BOOT_APP: u8 = 0x05;
const CMD_GET_VERSION: u8 = 0x06;

// Response codes
const RESP_ACK: u8 = 0x00;
const RESP_NACK: u8 = 0x01;
const RESP_BUSY: u8 = 0x02;

#[derive(Debug, Clone, Copy, PartialEq)]
enum BootloaderState {
    Idle,
    SessionActive,
    Erasing,
    Programming,
    Verifying,
}

struct BootloaderContext {
    base_address: u32,
    total_size: u32,
    bytes_received: u32,
    expected_crc: u32,
    state: BootloaderState,
    timeout_counter: u32,
}

impl BootloaderContext {
    fn new() -> Self {
        Self {
            base_address: APP_START_ADDRESS,
            total_size: 0,
            bytes_received: 0,
            expected_crc: 0,
            state: BootloaderState::Idle,
            timeout_counter: 0,
        }
    }
}

struct Bootloader {
    ctx: BootloaderContext,
}

impl Bootloader {
    fn new() -> Self {
        Self {
            ctx: BootloaderContext::new(),
        }
    }

    fn erase_flash(&mut self, flash: &mut UnlockedFlash) -> Result<(), ()> {
        // Calculate sectors to erase based on total_size
        let sectors_to_erase = (self.ctx.total_size + 0xFFFF) / 0x10000;
        
        for sector in 2..(2 + sectors_to_erase) {
            flash.erase(sector as u8).map_err(|_| ())?;
        }
        
        Ok(())
    }

    fn write_flash(
        &mut self,
        flash: &mut UnlockedFlash,
        offset: u32,
        data: &[u8],
    ) -> Result<(), ()> {
        let address = self.ctx.base_address + offset;
        
        // Flash must be written in aligned chunks
        for (i, chunk) in data.chunks(4).enumerate() {
            let mut word = 0u32;
            for (j, &byte) in chunk.iter().enumerate() {
                word |= (byte as u32) << (j * 8);
            }
            
            let write_addr = address + (i * 4) as u32;
            flash.program(write_addr, word).map_err(|_| ())?;
        }
        
        Ok(())
    }

    fn calculate_crc(&self) -> u32 {
        // Simple CRC32 implementation
        let mut crc: u32 = 0xFFFF_FFFF;
        let data_ptr = self.ctx.base_address as *const u8;
        
        for i in 0..self.ctx.total_size {
            let byte = unsafe { ptr::read_volatile(data_ptr.add(i as usize)) };
            crc ^= byte as u32;
            
            for _ in 0..8 {
                if crc & 1 != 0 {
                    crc = (crc >> 1) ^ 0xEDB8_8320;
                } else {
                    crc >>= 1;
                }
            }
        }
        
        !crc
    }

    fn jump_to_application(&self) -> ! {
        unsafe {
            let app_stack = ptr::read_volatile(APP_START_ADDRESS as *const u32);
            let app_entry = ptr::read_volatile((APP_START_ADDRESS + 4) as *const u32);
            
            // Validate stack pointer
            if (app_stack & 0xFFF0_0000) != 0x2000_0000 {
                panic!("Invalid application");
            }
            
            // Disable interrupts
            cortex_m::interrupt::disable();
            
            // Set vector table offset
            let scb = &*cortex_m::peripheral::SCB::ptr();
            scb.vtor.write(APP_START_ADDRESS);
            
            // Set stack pointer and jump
            cortex_m::asm::bootload(app_stack as *const u32);
        }
    }

    fn process_message<C: embedded_hal::can::Can>(
        &mut self,
        can: &mut C,
        flash: &mut LockedFlash,
        frame: &C::Frame,
    ) where
        C::Frame: Frame,
    {
        let data = frame.data();
        if data.is_empty() {
            return;
        }

        let cmd = data[0];

        match cmd {
            CMD_START_SESSION => {
                if data.len() >= 9 {
                    self.ctx.state = BootloaderState::SessionActive;
                    self.ctx.total_size = u32::from_le_bytes([
                        data[1], data[2], data[3], data[4]
                    ]);
                    self.ctx.expected_crc = u32::from_le_bytes([
                        data[5], data[6], data[7], data[8]
                    ]);
                    self.ctx.bytes_received = 0;
                    
                    self.send_response(can, CMD_START_SESSION, RESP_ACK, &[]);
                }
            }

            CMD_ERASE_FLASH => {
                if self.ctx.state == BootloaderState::SessionActive {
                    self.ctx.state = BootloaderState::Erasing;
                    self.send_response(can, CMD_ERASE_FLASH, RESP_BUSY, &[]);
                    
                    let mut unlocked = flash.unlocked();
                    match self.erase_flash(&mut unlocked) {
                        Ok(_) => {
                            self.ctx.state = BootloaderState::Programming;
                            self.send_response(can, CMD_ERASE_FLASH, RESP_ACK, &[]);
                        }
                        Err(_) => {
                            self.ctx.state = BootloaderState::Idle;
                            self.send_response(can, CMD_ERASE_FLASH, RESP_NACK, &[]);
                        }
                    }
                } else {
                    self.send_response(can, CMD_ERASE_FLASH, RESP_NACK, &[]);
                }
            }

            CMD_WRITE_DATA => {
                if self.ctx.state == BootloaderState::Programming && data.len() >= 4 {
                    let offset = u16::from_le_bytes([data[1], data[2]]) as u32;
                    let length = data[3] as usize;
                    let payload = &data[4..4 + length.min(data.len() - 4)];
                    
                    let mut unlocked = flash.unlocked();
                    match self.write_flash(&mut unlocked, offset, payload) {
                        Ok(_) => {
                            self.ctx.bytes_received += payload.len() as u32;
                            self.send_response(can, CMD_WRITE_DATA, RESP_ACK, &[]);
                        }
                        Err(_) => {
                            self.send_response(can, CMD_WRITE_DATA, RESP_NACK, &[]);
                        }
                    }
                } else {
                    self.send_response(can, CMD_WRITE_DATA, RESP_NACK, &[]);
                }
            }

            CMD_VERIFY => {
                self.ctx.state = BootloaderState::Verifying;
                let calculated_crc = self.calculate_crc();
                
                if calculated_crc == self.ctx.expected_crc {
                    let crc_bytes = calculated_crc.to_le_bytes();
                    self.send_response(can, CMD_VERIFY, RESP_ACK, &crc_bytes);
                } else {
                    let crc_bytes = calculated_crc.to_le_bytes();
                    self.send_response(can, CMD_VERIFY, RESP_NACK, &crc_bytes);
                }
            }

            CMD_BOOT_APP => {
                self.send_response(can, CMD_BOOT_APP, RESP_ACK, &[]);
                // Delay to allow message transmission
                cortex_m::asm::delay(1_000_000);
                self.jump_to_application();
            }

            CMD_GET_VERSION => {
                let version_bytes = BOOTLOADER_VERSION.to_le_bytes();
                self.send_response(can, CMD_GET_VERSION, RESP_ACK, &version_bytes);
            }

            _ => {
                self.send_response(can, cmd, RESP_NACK, &[]);
            }
        }
    }

    fn send_response<C: embedded_hal::can::Can>(
        &self,
        can: &mut C,
        cmd: u8,
        status: u8,
        data: &[u8],
    ) where
        C::Frame: Frame,
    {
        let mut tx_data = [0u8; 8];
        tx_data[0] = cmd;
        tx_data[1] = status;
        
        let copy_len = data.len().min(6);
        tx_data[2..2 + copy_len].copy_from_slice(&data[..copy_len]);
        
        // Create frame - implementation depends on the CAN HAL
        // This is a simplified example
        // let frame = C::Frame::new(
        //     Id::Standard(CAN_BOOTLOADER_ID),
        //     &tx_data[..2 + copy_len]
        // );
        // let _ = can.transmit(&frame);
    }
}

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::peripheral::Peripherals::take().unwrap();

    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    // Initialize CAN
    let gpioa = dp.GPIOA.split();
    let can_rx = gpioa.pa11.into_alternate();
    let can_tx = gpioa.pa12.into_alternate();

    let mut can = Can::new(dp.CAN1, (can_tx, can_rx));
    
    // Configure CAN at 500 kbps
    can.configure(|config| {
        config.set_bit_timing(0x001c_0003); // 500 kbps @ 42 MHz
        config.set_loopback(false);
        config.set_silent(false);
    });

    let flash = dp.FLASH.constrain();
    let mut bootloader = Bootloader::new();

    let mut delay = cp.SYST.delay(&clocks);
    let timeout_ms = 5000u32;
    let mut elapsed = 0u32;

    loop {
        // Check for CAN messages
        if let Ok(frame) = can.receive() {
            bootloader.process_message(&mut can, &mut flash, &frame);
            elapsed = 0; // Reset timeout on activity
        }

        // Timeout handling
        if bootloader.ctx.state == BootloaderState::Idle {
            elapsed += 1;
            if elapsed > timeout_ms {
                bootloader.jump_to_application();
            }
        }

        delay.delay_ms(1u32);
    }
}
```

### 3. Host-Side Update Tool (C++)

```cpp
// can_updater.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

class CANBootloaderClient {
private:
    int socket_fd;
    uint32_t bootloader_id;
    uint32_t host_id;
    
    uint32_t calculate_crc32(const std::vector<uint8_t>& data) {
        uint32_t crc = 0xFFFFFFFF;
        
        for (uint8_t byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; i++) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
            }
        }
        
        return ~crc;
    }
    
    bool send_frame(uint8_t cmd, const uint8_t* data, size_t len) {
        struct can_frame frame;
        frame.can_id = host_id;
        frame.can_dlc = len + 1;
        frame.data[0] = cmd;
        
        if (data && len > 0) {
            memcpy(&frame.data[1], data, std::min(len, (size_t)7));
        }
        
        ssize_t bytes = write(socket_fd, &frame, sizeof(frame));
        return bytes == sizeof(frame);
    }
    
    bool receive_frame(struct can_frame& frame, int timeout_ms = 1000) {
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(socket_fd, &read_fds);
        
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        
        int ret = select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ret <= 0) return false;
        
        ssize_t bytes = read(socket_fd, &frame, sizeof(frame));
        return bytes == sizeof(frame);
    }
    
public:
    CANBootloaderClient(const std::string& interface, 
                       uint32_t bl_id = 0x100, 
                       uint32_t h_id = 0x200) 
        : bootloader_id(bl_id), host_id(h_id) {
        
        socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (socket_fd < 0) {
            throw std::runtime_error("Failed to create CAN socket");
        }
        
        struct ifreq ifr;
        strcpy(ifr.ifr_name, interface.c_str());
        ioctl(socket_fd, SIOCGIFINDEX, &ifr);
        
        struct sockaddr_can addr;
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        
        if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(socket_fd);
            throw std::runtime_error("Failed to bind CAN socket");
        }
    }
    
    ~CANBootloaderClient() {
        if (socket_fd >= 0) {
            close(socket_fd);
        }
    }
    
    bool start_session(uint32_t firmware_size, uint32_t crc) {
        uint8_t data[8];
        memcpy(&data[0], &firmware_size, 4);
        memcpy(&data[4], &crc, 4);
        
        if (!send_frame(0x01, data, 8)) {
            return false;
        }
        
        struct can_frame frame;
        if (!receive_frame(frame)) {
            return false;
        }
        
        return frame.data[0] == 0x01 && frame.data[1] == 0x00; // ACK
    }
    
    bool erase_flash() {
        if (!send_frame(0x02, nullptr, 0)) {
            return false;
        }
        
        struct can_frame frame;
        // First response might be BUSY
        if (!receive_frame(frame, 5000)) {
            return false;
        }
        
        // Wait for final ACK
        if (frame.data[1] == 0x02) { // BUSY
            if (!receive_frame(frame, 10000)) {
                return false;
            }}
        
        return frame.data[0] == 0x02 && frame.data[1] == 0x00; // ACK
    }
    
    bool write_data(uint16_t offset, const uint8_t* data, uint8_t length) {
        uint8_t tx_data[8];
        memcpy(&tx_data[0], &offset, 2);
        tx_data[2] = length;
        memcpy(&tx_data[3], data, std::min((int)length, 4));
        
        if (!send_frame(0x03, tx_data, 3 + std::min((int)length, 4))) {
            return false;
        }
        
        struct can_frame frame;
        if (!receive_frame(frame)) {
            return false;
        }
        
        return frame.data[0] == 0x03 && frame.data[1] == 0x00; // ACK
    }
    
    bool verify(uint32_t expected_crc, uint32_t& actual_crc) {
        if (!send_frame(0x04, nullptr, 0)) {
            return false;
        }
        
        struct can_frame frame;
        if (!receive_frame(frame, 5000)) {
            return false;
        }
        
        if (frame.can_dlc >= 6) {
            memcpy(&actual_crc, &frame.data[2], 4);
        }
        
        return frame.data[0] == 0x04 && frame.data[1] == 0x00; // ACK
    }
    
    bool boot_application() {
        return send_frame(0x05, nullptr, 0);
    }
    
    bool update_firmware(const std::string& filename) {
        // Read firmware file
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open firmware file" << std::endl;
            return false;
        }
        
        std::vector<uint8_t> firmware((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        file.close();
        
        std::cout << "Firmware size: " << firmware.size() << " bytes" << std::endl;
        
        // Calculate CRC
        uint32_t crc = calculate_crc32(firmware);
        std::cout << "Firmware CRC32: 0x" << std::hex << crc << std::dec << std::endl;
        
        // Start session
        std::cout << "Starting update session..." << std::endl;
        if (!start_session(firmware.size(), crc)) {
            std::cerr << "Failed to start session" << std::endl;
            return false;
        }
        
        // Erase flash
        std::cout << "Erasing flash..." << std::endl;
        if (!erase_flash()) {
            std::cerr << "Failed to erase flash" << std::endl;
            return false;
        }
        
        // Write data in chunks
        std::cout << "Writing firmware..." << std::endl;
        const size_t chunk_size = 4; // Match CAN payload constraints
        size_t offset = 0;
        
        while (offset < firmware.size()) {
            size_t remaining = firmware.size() - offset;
            size_t write_size = std::min(remaining, chunk_size);
            
            if (!write_data(offset, &firmware[offset], write_size)) {
                std::cerr << "Failed to write at offset " << offset << std::endl;
                return false;
            }
            
            offset += write_size;
            
            if (offset % 1024 == 0 || offset == firmware.size()) {
                std::cout << "Progress: " << (offset * 100 / firmware.size()) 
                         << "%" << std::endl;
            }
        }
        
        // Verify
        std::cout << "Verifying firmware..." << std::endl;
        uint32_t actual_crc;
        if (!verify(crc, actual_crc)) {
            std::cerr << "Verification failed! Expected: 0x" << std::hex << crc 
                     << ", Got: 0x" << actual_crc << std::dec << std::endl;
            return false;
        }
        
        std::cout << "Firmware update successful!" << std::endl;
        
        // Boot application
        std::cout << "Booting application..." << std::endl;
        boot_application();
        
        return true;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <can_interface> <firmware_file>" 
                 << std::endl;
        return 1;
    }
    
    try {
        CANBootloaderClient client(argv[1]);
        
        if (client.update_firmware(argv[2])) {
            std::cout << "Update completed successfully!" << std::endl;
            return 0;
        } else {
            std::cerr << "Update failed!" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Summary

### Key Takeaways

**CAN Bootloader Essentials:**
- Enables remote firmware updates over CAN bus without physical access
- Requires careful memory partitioning between bootloader and application
- Must implement robust error handling and verification mechanisms

**Critical Design Elements:**
1. **Security**: CRC/checksum validation, optional cryptographic signatures
2. **Reliability**: Flash wear leveling, atomic updates, fallback mechanisms
3. **Protocol**: State machine-based command processing, timeout handling
4. **Memory Safety**: Protected bootloader region, validated application jumps

**Implementation Considerations:**
- **C/C++**: Direct hardware control, minimal overhead, suitable for resource-constrained devices
- **Rust**: Memory safety guarantees, zero-cost abstractions, modern embedded ecosystem
- **Protocol Design**: Keep messages within CAN payload limits (8 bytes), use chunked transfers for large data

**Best Practices:**
- Always validate firmware before executing (CRC, signatures)
- Implement watchdog timers and timeout mechanisms
- Preserve bootloader in protected flash sectors
- Support rollback to previous firmware on failure
- Log update attempts and outcomes for debugging
- Test extensively with interrupted/corrupted updates

This bootloader architecture provides a foundation for safe, reliable over-the-air updates in CAN-based embedded systems, critical for automotive and industrial applications.