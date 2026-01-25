# CAN Controller Integration

## Overview

CAN (Controller Area Network) is a robust vehicle bus standard designed to allow microcontrollers and devices to communicate with each other without a host computer. CAN controller integration involves interfacing with hardware controllers that handle the physical layer and data link layer of CAN communication.

## Common CAN Controllers

### 1. **MCP2515** (SPI-based standalone controller)
- External CAN controller with SPI interface
- Commonly used with microcontrollers lacking built-in CAN
- Requires external CAN transceiver (e.g., MCP2551)

### 2. **SJA1000** (Parallel/SPI interface)
- Legacy but widely used industrial CAN controller
- Supports both BasicCAN and PeliCAN modes
- Common in embedded Linux systems

### 3. **Integrated MCU Peripherals**
- STM32, ESP32, NXP LPC series with built-in CAN
- Direct hardware access with lower latency
- Often include multiple CAN channels

## CAN Frame Structure

```
Standard CAN Frame (CAN 2.0A):
┌─────────┬─────┬───┬────┬─────┬──────────────┬─────┬─────┬────┐
│   SOF   │ ID  │RTR│IDE │ r0  │     DLC      │ Data│ CRC │ACK │
│ 1 bit   │11bit│1b │ 1b │ 1b  │   4 bits     │0-8B │15bit│2bit│
└─────────┴─────┴───┴────┴─────┴──────────────┴─────┴─────┴────┘

Extended CAN Frame (CAN 2.0B):
- 29-bit identifier instead of 11-bit
- Additional IDE, SRR, r1 bits
```

## C/C++ Implementation Examples

### Example 1: MCP2515 Driver (Arduino/Embedded C++)

```cpp
#include <SPI.h>

// MCP2515 Register Definitions
#define MCP_CANCTRL   0x0F
#define MCP_CANSTAT   0x0E
#define MCP_TXB0CTRL  0x30
#define MCP_RXB0CTRL  0x60
#define MCP_CNF1      0x2A
#define MCP_CNF2      0x29
#define MCP_CNF3      0x28

// MCP2515 Commands
#define MCP_RESET     0xC0
#define MCP_READ      0x03
#define MCP_WRITE     0x02
#define MCP_RTS_TX0   0x81
#define MCP_READ_RX0  0x90
#define MCP_BITMOD    0x05

// Operation Modes
#define MODE_NORMAL   0x00
#define MODE_SLEEP    0x20
#define MODE_LOOPBACK 0x40
#define MODE_CONFIG   0x80

class MCP2515 {
private:
    uint8_t cs_pin;
    
    void spiWrite(uint8_t data) {
        SPI.transfer(data);
    }
    
    uint8_t spiRead() {
        return SPI.transfer(0x00);
    }
    
    void select() {
        digitalWrite(cs_pin, LOW);
    }
    
    void deselect() {
        digitalWrite(cs_pin, HIGH);
    }
    
    void reset() {
        select();
        spiWrite(MCP_RESET);
        deselect();
        delay(10);
    }
    
    void writeRegister(uint8_t address, uint8_t value) {
        select();
        spiWrite(MCP_WRITE);
        spiWrite(address);
        spiWrite(value);
        deselect();
    }
    
    uint8_t readRegister(uint8_t address) {
        select();
        spiWrite(MCP_READ);
        spiWrite(address);
        uint8_t value = spiRead();
        deselect();
        return value;
    }
    
    void modifyRegister(uint8_t address, uint8_t mask, uint8_t value) {
        select();
        spiWrite(MCP_BITMOD);
        spiWrite(address);
        spiWrite(mask);
        spiWrite(value);
        deselect();
    }
    
    void setMode(uint8_t mode) {
        modifyRegister(MCP_CANCTRL, 0xE0, mode);
        delay(10);
    }

public:
    MCP2515(uint8_t cs) : cs_pin(cs) {}
    
    bool begin(uint32_t baudrate = 500000) {
        pinMode(cs_pin, OUTPUT);
        deselect();
        SPI.begin();
        
        reset();
        
        // Enter configuration mode
        setMode(MODE_CONFIG);
        
        // Configure bit timing for 500kbps @ 16MHz
        if (baudrate == 500000) {
            writeRegister(MCP_CNF1, 0x00);  // SJW=1, BRP=0
            writeRegister(MCP_CNF2, 0x90);  // BTLMODE=1, SAM=0, PS1=2, PRSEG=0
            writeRegister(MCP_CNF3, 0x02);  // PS2=2
        }
        
        // Configure receive buffer
        writeRegister(MCP_RXB0CTRL, 0x60);  // Accept all messages
        
        // Enter normal mode
        setMode(MODE_NORMAL);
        
        // Verify mode
        uint8_t mode_check = readRegister(MCP_CANSTAT) & 0xE0;
        return (mode_check == MODE_NORMAL);
    }
    
    bool sendMessage(uint32_t id, const uint8_t* data, uint8_t length, bool extended = false) {
        if (length > 8) return false;
        
        // Wait for transmit buffer to be free
        uint8_t ctrl = readRegister(MCP_TXB0CTRL);
        if (ctrl & 0x08) return false;  // TX request bit set
        
        select();
        spiWrite(MCP_WRITE);
        spiWrite(0x31);  // Start at TXB0SIDH
        
        if (extended) {
            // 29-bit identifier
            spiWrite((uint8_t)(id >> 21));           // SIDH
            spiWrite((uint8_t)(((id >> 13) & 0xE0) | 0x08 | ((id >> 16) & 0x03))); // SIDL
            spiWrite((uint8_t)(id >> 8));            // EID8
            spiWrite((uint8_t)id);                   // EID0
        } else {
            // 11-bit identifier
            spiWrite((uint8_t)(id >> 3));            // SIDH
            spiWrite((uint8_t)(id << 5));            // SIDL
            spiWrite(0x00);                          // EID8
            spiWrite(0x00);                          // EID0
        }
        
        spiWrite(length);  // DLC
        
        // Write data bytes
        for (uint8_t i = 0; i < length; i++) {
            spiWrite(data[i]);
        }
        
        deselect();
        
        // Request transmission
        select();
        spiWrite(MCP_RTS_TX0);
        deselect();
        
        return true;
    }
    
    bool receiveMessage(uint32_t& id, uint8_t* data, uint8_t& length, bool& extended) {
        // Check if message available
        uint8_t status = readRegister(MCP_CANSTAT);
        if ((status & 0x03) == 0) return false;  // No message
        
        select();
        spiWrite(MCP_READ_RX0);
        
        uint8_t sidh = spiRead();
        uint8_t sidl = spiRead();
        uint8_t eid8 = spiRead();
        uint8_t eid0 = spiRead();
        
        extended = (sidl & 0x08) != 0;
        
        if (extended) {
            id = ((uint32_t)sidh << 21) |
                 ((uint32_t)(sidl & 0xE0) << 13) |
                 ((uint32_t)(sidl & 0x03) << 16) |
                 ((uint32_t)eid8 << 8) |
                 eid0;
        } else {
            id = ((uint32_t)sidh << 3) | (sidl >> 5);
        }
        
        uint8_t dlc = spiRead() & 0x0F;
        length = dlc;
        
        for (uint8_t i = 0; i < length; i++) {
            data[i] = spiRead();
        }
        
        deselect();
        
        // Clear interrupt flag
        modifyRegister(MCP_CANINTF, 0x01, 0x00);
        
        return true;
    }
};

// Usage example
void setup() {
    Serial.begin(115200);
    
    MCP2515 can(10);  // CS pin 10
    
    if (can.begin(500000)) {
        Serial.println("CAN initialized successfully");
    } else {
        Serial.println("CAN initialization failed");
    }
}

void loop() {
    MCP2515 can(10);
    
    // Send message
    uint8_t tx_data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    can.sendMessage(0x123, tx_data, 8, false);
    
    // Receive message
    uint32_t rx_id;
    uint8_t rx_data[8];
    uint8_t rx_length;
    bool extended;
    
    if (can.receiveMessage(rx_id, rx_data, rx_length, extended)) {
        Serial.print("Received ID: 0x");
        Serial.print(rx_id, HEX);
        Serial.print(" Data: ");
        for (uint8_t i = 0; i < rx_length; i++) {
            Serial.print(rx_data[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }
    
    delay(100);
}
```

### Example 2: STM32 Built-in CAN (HAL Library)

```cpp
#include "stm32f4xx_hal.h"

CAN_HandleTypeDef hcan1;

// CAN initialization
void CAN_Init(void) {
    // Configure CAN peripheral
    hcan1.Instance = CAN1;
    hcan1.Init.Prescaler = 6;  // 500kbps @ 42MHz APB1
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1 = CAN_BS1_11TQ;
    hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan1.Init.TimeTriggeredMode = DISABLE;
    hcan1.Init.AutoBusOff = DISABLE;
    hcan1.Init.AutoWakeUp = DISABLE;
    hcan1.Init.AutoRetransmission = ENABLE;
    hcan1.Init.ReceiveFifoLocked = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;
    
    if (HAL_CAN_Init(&hcan1) != HAL_OK) {
        Error_Handler();
    }
    
    // Configure filter to accept all messages
    CAN_FilterTypeDef filter;
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x0000;
    filter.FilterIdLow = 0x0000;
    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;
    
    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) {
        Error_Handler();
    }
    
    // Start CAN
    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        Error_Handler();
    }
    
    // Activate notifications
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        Error_Handler();
    }
}

// Send CAN message
HAL_StatusTypeDef CAN_SendMessage(uint32_t id, uint8_t* data, uint8_t length) {
    CAN_TxHeaderTypeDef tx_header;
    uint32_t tx_mailbox;
    
    tx_header.StdId = id;
    tx_header.ExtId = 0;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.IDE = CAN_ID_STD;
    tx_header.DLC = length;
    tx_header.TransmitGlobalTime = DISABLE;
    
    return HAL_CAN_AddTxMessage(&hcan1, &tx_header, data, &tx_mailbox);
}

// Receive CAN message (polling)
HAL_StatusTypeDef CAN_ReceiveMessage(uint32_t* id, uint8_t* data, uint8_t* length) {
    CAN_RxHeaderTypeDef rx_header;
    
    if (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) == 0) {
        return HAL_ERROR;
    }
    
    HAL_StatusTypeDef status = HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx_header, data);
    
    if (status == HAL_OK) {
        *id = (rx_header.IDE == CAN_ID_STD) ? rx_header.StdId : rx_header.ExtId;
        *length = rx_header.DLC;
    }
    
    return status;
}

// Interrupt callback
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
    CAN_RxHeaderTypeDef rx_header;
    uint8_t rx_data[8];
    
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK) {
        // Process received message
        uint32_t id = (rx_header.IDE == CAN_ID_STD) ? rx_header.StdId : rx_header.ExtId;
        uint8_t length = rx_header.DLC;
        
        // Your message handling code here
    }
}

// Main application
int main(void) {
    HAL_Init();
    SystemClock_Config();
    
    CAN_Init();
    
    uint8_t tx_data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    
    while (1) {
        // Send message
        CAN_SendMessage(0x456, tx_data, 8);
        
        // Receive message (polling mode)
        uint32_t rx_id;
        uint8_t rx_data[8];
        uint8_t rx_length;
        
        if (CAN_ReceiveMessage(&rx_id, rx_data, &rx_length) == HAL_OK) {
            // Process received message
        }
        
        HAL_Delay(100);
    }
}
```

### Example 3: SocketCAN (Linux)

```cpp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

class SocketCAN {
private:
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;
    
public:
    SocketCAN() : sock(-1) {}
    
    ~SocketCAN() {
        if (sock >= 0) {
            close(sock);
        }
    }
    
    bool begin(const char* interface = "can0") {
        // Create socket
        sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock < 0) {
            perror("Socket creation failed");
            return false;
        }
        
        // Get interface index
        strcpy(ifr.ifr_name, interface);
        if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
            perror("ioctl failed");
            return false;
        }
        
        // Bind socket to CAN interface
        memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Bind failed");
            return false;
        }
        
        return true;
    }
    
    bool sendMessage(uint32_t id, const uint8_t* data, uint8_t length, bool extended = false) {
        struct can_frame frame;
        
        if (length > 8) return false;
        
        frame.can_id = id;
        if (extended) {
            frame.can_id |= CAN_EFF_FLAG;
        }
        
        frame.can_dlc = length;
        memcpy(frame.data, data, length);
        
        ssize_t bytes_sent = write(sock, &frame, sizeof(frame));
        return (bytes_sent == sizeof(frame));
    }
    
    bool receiveMessage(uint32_t& id, uint8_t* data, uint8_t& length, bool& extended) {
        struct can_frame frame;
        
        ssize_t bytes_read = read(sock, &frame, sizeof(frame));
        if (bytes_read != sizeof(frame)) {
            return false;
        }
        
        extended = (frame.can_id & CAN_EFF_FLAG) != 0;
        id = frame.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        length = frame.can_dlc;
        memcpy(data, frame.data, length);
        
        return true;
    }
    
    bool setFilter(uint32_t id, uint32_t mask, bool extended = false) {
        struct can_filter filter;
        
        filter.can_id = id;
        if (extended) {
            filter.can_id |= CAN_EFF_FLAG;
        }
        
        filter.can_mask = mask;
        if (extended) {
            filter.can_mask |= CAN_EFF_FLAG;
        }
        
        if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) < 0) {
            perror("Filter setup failed");
            return false;
        }
        
        return true;
    }
};

// Usage example
int main() {
    SocketCAN can;
    
    if (!can.begin("can0")) {
        fprintf(stderr, "Failed to initialize CAN\n");
        return 1;
    }
    
    printf("CAN initialized successfully\n");
    
    // Send a message
    uint8_t tx_data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    if (can.sendMessage(0x123, tx_data, 8, false)) {
        printf("Message sent successfully\n");
    }
    
    // Receive messages
    while (1) {
        uint32_t rx_id;
        uint8_t rx_data[8];
        uint8_t rx_length;
        bool extended;
        
        if (can.receiveMessage(rx_id, rx_data, rx_length, extended)) {
            printf("Received ID: 0x%03X%s, Length: %d, Data: ",
                   rx_id, extended ? " (ext)" : "", rx_length);
            
            for (int i = 0; i < rx_length; i++) {
                printf("%02X ", rx_data[i]);
            }
            printf("\n");
        }
    }
    
    return 0;
}
```

## Rust Implementation Examples

### Example 1: SocketCAN with Rust

```rust
use std::io;
use std::time::Duration;

// Using socketcan crate
use socketcan::{CANSocket, CANFrame, CANSocketOpenError};

pub struct CanInterface {
    socket: CANSocket,
}

impl CanInterface {
    /// Open a CAN interface
    pub fn new(interface: &str) -> Result<Self, CANSocketOpenError> {
        let socket = CANSocket::open(interface)?;
        Ok(CanInterface { socket })
    }
    
    /// Send a CAN frame
    pub fn send(&self, id: u32, data: &[u8], extended: bool) -> io::Result<()> {
        if data.len() > 8 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Data length exceeds 8 bytes"
            ));
        }
        
        let frame = if extended {
            CANFrame::new(id, data, false, false)?
        } else {
            CANFrame::new(id, data, false, false)?
        };
        
        self.socket.write_frame(&frame)?;
        Ok(())
    }
    
    /// Receive a CAN frame
    pub fn receive(&self) -> io::Result<CANFrame> {
        self.socket.read_frame()
    }
    
    /// Set a timeout for receive operations
    pub fn set_read_timeout(&self, timeout: Option<Duration>) -> io::Result<()> {
        self.socket.set_read_timeout(timeout)
    }
    
    /// Set a timeout for send operations
    pub fn set_write_timeout(&self, timeout: Option<Duration>) -> io::Result<()> {
        self.socket.set_write_timeout(timeout)
    }
}

// Higher-level message handling
#[derive(Debug, Clone)]
pub struct CanMessage {
    pub id: u32,
    pub data: Vec<u8>,
    pub extended: bool,
    pub remote: bool,
}

impl CanMessage {
    pub fn new(id: u32, data: Vec<u8>, extended: bool) -> Self {
        CanMessage {
            id,
            data,
            extended,
            remote: false,
        }
    }
    
    pub fn from_frame(frame: &CANFrame) -> Self {
        CanMessage {
            id: frame.id(),
            data: frame.data().to_vec(),
            extended: frame.is_extended(),
            remote: frame.is_rtr(),
        }
    }
}

// Example: CAN bus manager with filtering
pub struct CanBusManager {
    interface: CanInterface,
    filters: Vec<(u32, u32)>, // (id, mask) pairs
}

impl CanBusManager {
    pub fn new(interface_name: &str) -> Result<Self, CANSocketOpenError> {
        let interface = CanInterface::new(interface_name)?;
        Ok(CanBusManager {
            interface,
            filters: Vec::new(),
        })
    }
    
    pub fn add_filter(&mut self, id: u32, mask: u32) {
        self.filters.push((id, mask));
    }
    
    pub fn send_message(&self, msg: &CanMessage) -> io::Result<()> {
        self.interface.send(msg.id, &msg.data, msg.extended)
    }
    
    pub fn receive_message(&self) -> io::Result<CanMessage> {
        loop {
            let frame = self.interface.receive()?;
            let msg = CanMessage::from_frame(&frame);
            
            // Apply filters if any
            if self.filters.is_empty() {
                return Ok(msg);
            }
            
            for (filter_id, mask) in &self.filters {
                if (msg.id & mask) == (filter_id & mask) {
                    return Ok(msg);
                }
            }
        }
    }
}

// Usage example
fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize CAN interface
    let mut can = CanBusManager::new("can0")?;
    
    // Add filter to receive only messages with ID 0x100-0x1FF
    can.add_filter(0x100, 0xFF00);
    
    // Send a message
    let tx_msg = CanMessage::new(
        0x123,
        vec![0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08],
        false,
    );
    
    can.send_message(&tx_msg)?;
    println!("Sent message: ID=0x{:03X}, Data={:02X?}", tx_msg.id, tx_msg.data);
    
    // Receive messages
    loop {
        match can.receive_message() {
            Ok(rx_msg) => {
                println!(
                    "Received: ID=0x{:03X}{}, Length={}, Data={:02X?}",
                    rx_msg.id,
                    if rx_msg.extended { " (ext)" } else { "" },
                    rx_msg.data.len(),
                    rx_msg.data
                );
            }
            Err(e) => {
                eprintln!("Receive error: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}
```

### Example 2: Embedded Rust with Embassy (STM32)

```rust
#![no_std]
#![no_main]

use defmt::*;
use embassy_executor::Spawner;
use embassy_stm32::can::{Can, Envelope, Frame, StandardId};
use embassy_stm32::{bind_interrupts, can, peripherals, Config};
use embassy_time::{Duration, Timer};
use {defmt_rtt as _, panic_probe as _};

bind_interrupts!(struct Irqs {
    CAN1_TX => can::TxInterruptHandler<peripherals::CAN1>;
    CAN1_RX0 => can::Rx0InterruptHandler<peripherals::CAN1>;
    CAN1_RX1 => can::Rx1InterruptHandler<peripherals::CAN1>;
    CAN1_SCE => can::SceInterruptHandler<peripherals::CAN1>;
});

#[embassy_executor::task]
async fn can_rx_task(mut can: Can<'static, peripherals::CAN1>) {
    loop {
        match can.read().await {
            Ok(envelope) => {
                let frame = envelope.frame();
                
                let id = match frame.id() {
                    can::Id::Standard(std_id) => std_id.as_raw() as u32,
                    can::Id::Extended(ext_id) => ext_id.as_raw(),
                };
                
                info!(
                    "RX: ID=0x{:03X}, Data={:02X}",
                    id,
                    frame.data()
                );
            }
            Err(e) => {
                error!("CAN RX error: {:?}", e);
            }
        }
    }
}

#[embassy_executor::task]
async fn can_tx_task(mut can: Can<'static, peripherals::CAN1>) {
    let mut counter: u8 = 0;
    
    loop {
        let data = [counter, counter.wrapping_add(1), counter.wrapping_add(2),
                   counter.wrapping_add(3), counter.wrapping_add(4),
                   counter.wrapping_add(5), counter.wrapping_add(6),
                   counter.wrapping_add(7)];
        
        let frame = Frame::new_data(
            StandardId::new(0x123).unwrap(),
            &data,
        ).unwrap();
        
        match can.write(&frame).await {
            Ok(()) => {
                info!("TX: ID=0x123, Counter={}", counter);
            }
            Err(e) => {
                error!("CAN TX error: {:?}", e);
            }
        }
        
        counter = counter.wrapping_add(1);
        Timer::after(Duration::from_millis(1000)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let mut config = Config::default();
    {
        use embassy_stm32::rcc::*;
        config.rcc.hse = Some(Hse {
            freq: embassy_stm32::time::Hertz(8_000_000),
            mode: HseMode::Oscillator,
        });
        config.rcc.pll_src = PllSource::HSE;
        config.rcc.pll = Some(Pll {
            prediv: PllPreDiv::DIV4,
            mul: PllMul::MUL168,
            divp: Some(PllPDiv::DIV2),
            divq: Some(PllQDiv::DIV7),
            divr: None,
        });
        config.rcc.ahb_pre = AHBPrescaler::DIV1;
        config.rcc.apb1_pre = APBPrescaler::DIV4;
        config.rcc.apb2_pre = APBPrescaler::DIV2;
        config.rcc.sys = Sysclk::PLL1_P;
    }
    
    let p = embassy_stm32::init(config);
    
    // Configure CAN at 500kbps
    let mut can_config = can::Config::default();
    can_config.loopback = false;
    can_config.silent = false;
    
    let mut can = Can::new(p.CAN1, p.PA11, p.PA12, Irqs);
    
    can.as_mut()
        .modify_config()
        .set_bit_timing(0x001c0003) // 500kbps @ 42MHz
        .enable();
    
    // Split CAN into TX and RX
    let (tx, rx) = can.split();
    
    // Spawn tasks
    spawner.spawn(can_rx_task(rx)).unwrap();
    spawner.spawn(can_tx_task(tx)).unwrap