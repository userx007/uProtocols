# SAP Service Access Points in Profibus

## Overview

Service Access Points (SAPs) are logical addressing mechanisms in Profibus that enable communication with specific services or applications within a station. Think of SAPs as "mailboxes" within a device - while the station address tells you which building to go to, the SAP tells you which specific mailbox within that building to deliver your message to.

## Technical Details

In Profibus, a station can host multiple services or applications. SAPs provide a way to distinguish between these different services at the Data Link Layer (Layer 2). Each SAP is identified by a number, allowing multiple concurrent communication relationships to exist within a single device.

### Key Concepts:

- **SAP Address**: Typically an 8-bit value (0-255) identifying a specific service
- **Default SAP**: SAP 0 is often used for general management services
- **Application SAPs**: Higher numbered SAPs (typically 1-127) for user applications
- **LSAP (Link Service Access Point)**: The Profibus term for SAP at the data link layer

SAPs are crucial in:
- Multi-master systems where stations need to communicate with different services
- Acyclic communication for parameter setting, diagnostics, and configuration
- Separating different communication streams within one device

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdio.h>

// SAP definitions
#define SAP_DEFAULT         0x00
#define SAP_MANAGEMENT      0x01
#define SAP_USER_APP1       0x3E
#define SAP_USER_APP2       0x3F
#define SAP_BROADCAST       0xFF

// Profibus telegram structure with SAP
typedef struct {
    uint8_t start_delimiter;
    uint8_t length;
    uint8_t dest_address;
    uint8_t source_address;
    uint8_t dest_sap;        // Destination SAP
    uint8_t source_sap;      // Source SAP
    uint8_t function_code;
    uint8_t data[256];
    uint16_t checksum;
} profibus_telegram_t;

// Function to send data to a specific SAP
int profibus_send_to_sap(uint8_t dest_addr, uint8_t dest_sap, 
                         uint8_t source_sap, const uint8_t *data, 
                         uint8_t data_len) {
    profibus_telegram_t telegram;
    
    telegram.start_delimiter = 0x68;  // SD2 - variable length
    telegram.length = data_len + 6;   // Header + data
    telegram.dest_address = dest_addr;
    telegram.source_address = 0x01;   // Our address
    telegram.dest_sap = dest_sap;
    telegram.source_sap = source_sap;
    telegram.function_code = 0x5C;    // SDN (Send Data with No acknowledge)
    
    // Copy data
    for (int i = 0; i < data_len; i++) {
        telegram.data[i] = data[i];
    }
    
    // Calculate checksum (simplified)
    telegram.checksum = calculate_checksum(&telegram);
    
    printf("Sending to Station %d, SAP %d from SAP %d\n", 
           dest_addr, dest_sap, source_sap);
    
    // Send telegram (hardware-specific code would go here)
    return send_telegram(&telegram);
}

// SAP router - dispatches incoming messages to appropriate handlers
typedef void (*sap_handler_t)(const uint8_t *data, uint8_t len);

typedef struct {
    uint8_t sap_number;
    sap_handler_t handler;
} sap_route_t;

sap_route_t sap_routing_table[128];
int sap_route_count = 0;

void register_sap_handler(uint8_t sap, sap_handler_t handler) {
    if (sap_route_count < 128) {
        sap_routing_table[sap_route_count].sap_number = sap;
        sap_routing_table[sap_route_count].handler = handler;
        sap_route_count++;
        printf("Registered handler for SAP %d\n", sap);
    }
}

void dispatch_to_sap(profibus_telegram_t *telegram) {
    uint8_t target_sap = telegram->dest_sap;
    
    for (int i = 0; i < sap_route_count; i++) {
        if (sap_routing_table[i].sap_number == target_sap) {
            printf("Dispatching to SAP %d handler\n", target_sap);
            sap_routing_table[i].handler(telegram->data, telegram->length);
            return;
        }
    }
    
    printf("Warning: No handler for SAP %d\n", target_sap);
}

// Example SAP handlers
void management_sap_handler(const uint8_t *data, uint8_t len) {
    printf("Management SAP: Processing %d bytes\n", len);
    // Handle management requests (reset, diagnostics, etc.)
}

void user_app1_handler(const uint8_t *data, uint8_t len) {
    printf("User Application 1: Processing %d bytes\n", len);
    // Handle application-specific data
}

int main() {
    // Register SAP handlers
    register_sap_handler(SAP_MANAGEMENT, management_sap_handler);
    register_sap_handler(SAP_USER_APP1, user_app1_handler);
    
    // Send data to specific SAP
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    profibus_send_to_sap(0x05, SAP_USER_APP1, SAP_DEFAULT, data, 4);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;

// SAP constants
const SAP_DEFAULT: u8 = 0x00;
const SAP_MANAGEMENT: u8 = 0x01;
const SAP_USER_APP1: u8 = 0x3E;
const SAP_USER_APP2: u8 = 0x3F;
const SAP_BROADCAST: u8 = 0xFF;

// Profibus telegram structure
#[derive(Debug, Clone)]
struct ProfbusTelegram {
    start_delimiter: u8,
    length: u8,
    dest_address: u8,
    source_address: u8,
    dest_sap: u8,
    source_sap: u8,
    function_code: u8,
    data: Vec<u8>,
    checksum: u16,
}

impl ProfbusTelegram {
    fn new(dest_addr: u8, dest_sap: u8, source_sap: u8, data: Vec<u8>) -> Self {
        let length = (data.len() + 6) as u8;
        
        let mut telegram = ProfbusTelegram {
            start_delimiter: 0x68,
            length,
            dest_address: dest_addr,
            source_address: 0x01,
            dest_sap,
            source_sap,
            function_code: 0x5C,
            data,
            checksum: 0,
        };
        
        telegram.checksum = telegram.calculate_checksum();
        telegram
    }
    
    fn calculate_checksum(&self) -> u16 {
        // Simplified checksum calculation
        let mut sum: u16 = 0;
        sum = sum.wrapping_add(self.dest_address as u16);
        sum = sum.wrapping_add(self.source_address as u16);
        sum = sum.wrapping_add(self.dest_sap as u16);
        sum = sum.wrapping_add(self.source_sap as u16);
        sum = sum.wrapping_add(self.function_code as u16);
        
        for byte in &self.data {
            sum = sum.wrapping_add(*byte as u16);
        }
        
        sum
    }
}

// SAP handler trait
trait SapHandler: Send + Sync {
    fn handle(&self, data: &[u8]) -> Result<(), String>;
}

// Management SAP handler
struct ManagementSapHandler;

impl SapHandler for ManagementSapHandler {
    fn handle(&self, data: &[u8]) -> Result<(), String> {
        println!("Management SAP: Processing {} bytes", data.len());
        // Process management commands
        Ok(())
    }
}

// User application SAP handler
struct UserApp1Handler;

impl SapHandler for UserApp1Handler {
    fn handle(&self, data: &[u8]) -> Result<(), String> {
        println!("User Application 1: Processing {} bytes", data.len());
        println!("Data: {:02X?}", data);
        // Process application-specific data
        Ok(())
    }
}

// SAP Router
struct SapRouter {
    handlers: HashMap<u8, Box<dyn SapHandler>>,
}

impl SapRouter {
    fn new() -> Self {
        SapRouter {
            handlers: HashMap::new(),
        }
    }
    
    fn register_handler(&mut self, sap: u8, handler: Box<dyn SapHandler>) {
        println!("Registering handler for SAP {}", sap);
        self.handlers.insert(sap, handler);
    }
    
    fn dispatch(&self, telegram: &ProfbusTelegram) -> Result<(), String> {
        let sap = telegram.dest_sap;
        
        match self.handlers.get(&sap) {
            Some(handler) => {
                println!("Dispatching to SAP {} handler", sap);
                handler.handle(&telegram.data)
            }
            None => {
                let msg = format!("No handler registered for SAP {}", sap);
                println!("Warning: {}", msg);
                Err(msg)
            }
        }
    }
}

// Profibus communication manager
struct ProfibusComm {
    router: SapRouter,
    station_address: u8,
}

impl ProfibusComm {
    fn new(station_address: u8) -> Self {
        ProfibusComm {
            router: SapRouter::new(),
            station_address,
        }
    }
    
    fn register_sap(&mut self, sap: u8, handler: Box<dyn SapHandler>) {
        self.router.register_handler(sap, handler);
    }
    
    fn send_to_sap(
        &self,
        dest_addr: u8,
        dest_sap: u8,
        source_sap: u8,
        data: Vec<u8>,
    ) -> Result<(), String> {
        let telegram = ProfbusTelegram::new(dest_addr, dest_sap, source_sap, data);
        
        println!(
            "Sending to Station {}, SAP {} from SAP {}",
            dest_addr, dest_sap, source_sap
        );
        
        // Hardware-specific send would go here
        self.send_telegram(&telegram)
    }
    
    fn send_telegram(&self, telegram: &ProfbusTelegram) -> Result<(), String> {
        // Simulate sending
        println!("Telegram sent: {:?}", telegram);
        Ok(())
    }
    
    fn receive_telegram(&self, telegram: ProfbusTelegram) -> Result<(), String> {
        if telegram.dest_address == self.station_address {
            self.router.dispatch(&telegram)
        } else {
            Err("Telegram not for this station".to_string())
        }
    }
}

fn main() {
    // Create communication manager
    let mut comm = ProfibusComm::new(0x01);
    
    // Register SAP handlers
    comm.register_sap(SAP_MANAGEMENT, Box::new(ManagementSapHandler));
    comm.register_sap(SAP_USER_APP1, Box::new(UserApp1Handler));
    
    // Send data to specific SAP
    let data = vec![0x01, 0x02, 0x03, 0x04, 0x05];
    match comm.send_to_sap(0x05, SAP_USER_APP1, SAP_DEFAULT, data) {
        Ok(_) => println!("Data sent successfully"),
        Err(e) => println!("Send error: {}", e),
    }
    
    // Simulate receiving a telegram
    let incoming = ProfbusTelegram::new(
        0x01,
        SAP_USER_APP1,
        SAP_DEFAULT,
        vec![0xAA, 0xBB, 0xCC],
    );
    
    match comm.receive_telegram(incoming) {
        Ok(_) => println!("Telegram processed successfully"),
        Err(e) => println!("Processing error: {}", e),
    }
}
```

## Summary

Service Access Points (SAPs) provide logical addressing within Profibus stations, enabling multiple services or applications to coexist on a single device. They function at the data link layer, allowing precise routing of messages to specific handlers within a station. SAPs are essential for:

- **Service Multiplexing**: Running multiple concurrent services on one device
- **Message Routing**: Directing telegrams to appropriate internal handlers
- **Protocol Layering**: Implementing clean separation between management, diagnostics, and application data

Implementation involves registering handlers for specific SAP numbers and dispatching incoming telegrams based on their destination SAP field. Both examples demonstrate a routing table pattern where SAP numbers map to handler functions, providing a clean architecture for multi-service Profibus devices. This mechanism is particularly important in complex systems with master-slave or multi-master configurations where devices need to handle various types of communication simultaneously.