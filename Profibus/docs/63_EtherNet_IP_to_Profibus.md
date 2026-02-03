# EtherNet/IP to Profibus Gateway Integration

## Detailed Description

EtherNet/IP to Profibus gateways serve as protocol converters that enable communication between Allen-Bradley/Rockwell Automation devices (which use EtherNet/IP) and Siemens/other Profibus-based systems. This integration is critical in heterogeneous industrial environments where equipment from different manufacturers must work together.

### Key Concepts

**Protocol Translation**: The gateway translates between two fundamentally different industrial protocols:
- **EtherNet/IP**: An industrial Ethernet protocol based on CIP (Common Industrial Protocol), using TCP/IP and UDP/IP
- **Profibus DP/PA**: A fieldbus protocol using RS-485 or fiber optic physical layers with token-passing access control

**Common Use Cases**:
- Integrating Allen-Bradley PLCs with Siemens automation systems
- Connecting Rockwell I/O modules to Profibus networks
- Bridging legacy Profibus systems with modern EtherNet/IP infrastructure
- Multi-vendor automation projects requiring interoperability

**Gateway Functions**:
- Bi-directional data exchange
- Protocol stack conversion
- Data mapping and buffering
- Cyclic and acyclic communication handling
- Diagnostic information forwarding

## Programming Examples

### C/C++ Example - EtherNet/IP Client to Profibus Gateway

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// EtherNet/IP CIP structures
typedef struct {
    uint8_t service;
    uint8_t request_path_size;
    uint16_t class_id;
    uint16_t instance_id;
    uint16_t attribute_id;
} CIP_Request;

typedef struct {
    uint8_t service;
    uint8_t reserved;
    uint8_t status;
    uint8_t additional_status_size;
    uint16_t data_length;
    uint8_t* data;
} CIP_Response;

// Profibus DP structures
typedef struct {
    uint8_t station_address;
    uint8_t function_code;
    uint8_t data_length;
    uint8_t data[244]; // Max DP telegram data
    uint8_t fcs; // Frame Check Sequence
} Profibus_DP_Frame;

// Gateway mapping structure
typedef struct {
    uint16_t ethernet_ip_instance;
    uint8_t profibus_slave_addr;
    uint16_t profibus_offset;
    uint16_t data_size;
    uint8_t* buffer;
} Gateway_Mapping;

// Gateway context
typedef struct {
    int ethernet_ip_socket;
    int profibus_fd;
    Gateway_Mapping* mappings;
    int mapping_count;
    uint8_t running;
} Gateway_Context;

// Initialize gateway mapping
Gateway_Mapping* create_mapping(uint16_t eip_instance, 
                                uint8_t pb_slave, 
                                uint16_t pb_offset, 
                                uint16_t size) {
    Gateway_Mapping* mapping = malloc(sizeof(Gateway_Mapping));
    if (!mapping) return NULL;
    
    mapping->ethernet_ip_instance = eip_instance;
    mapping->profibus_slave_addr = pb_slave;
    mapping->profibus_offset = pb_offset;
    mapping->data_size = size;
    mapping->buffer = calloc(size, sizeof(uint8_t));
    
    return mapping;
}

// Read data from EtherNet/IP device
int read_ethernet_ip_data(int socket, uint16_t instance, 
                          uint8_t* data, uint16_t* length) {
    CIP_Request request;
    request.service = 0x0E; // Get_Attribute_Single
    request.request_path_size = 3;
    request.class_id = 0x04; // Assembly class
    request.instance_id = instance;
    request.attribute_id = 0x03; // Data attribute
    
    // Send CIP request (simplified)
    uint8_t send_buffer[64];
    memcpy(send_buffer, &request, sizeof(CIP_Request));
    
    if (send(socket, send_buffer, sizeof(CIP_Request), 0) < 0) {
        perror("EtherNet/IP send failed");
        return -1;
    }
    
    // Receive response
    uint8_t recv_buffer[512];
    int received = recv(socket, recv_buffer, sizeof(recv_buffer), 0);
    if (received < 0) {
        perror("EtherNet/IP receive failed");
        return -1;
    }
    
    CIP_Response* response = (CIP_Response*)recv_buffer;
    if (response->status == 0) {
        *length = response->data_length;
        memcpy(data, response->data, response->data_length);
        return 0;
    }
    
    return -1;
}

// Write data to Profibus DP slave
int write_profibus_data(int fd, uint8_t slave_addr, 
                        uint16_t offset, uint8_t* data, 
                        uint16_t length) {
    Profibus_DP_Frame frame;
    frame.station_address = slave_addr;
    frame.function_code = 0x5D; // SDN - Send Data with No Acknowledge
    frame.data_length = length + 2; // +2 for offset
    
    // Pack offset and data
    frame.data[0] = (offset >> 8) & 0xFF;
    frame.data[1] = offset & 0xFF;
    memcpy(&frame.data[2], data, length);
    
    // Calculate FCS (simplified - actual requires proper calculation)
    frame.fcs = 0;
    for (int i = 0; i < length + 2; i++) {
        frame.fcs ^= frame.data[i];
    }
    
    // Send frame
    uint8_t send_buffer[256];
    int frame_size = 0;
    send_buffer[frame_size++] = frame.station_address;
    send_buffer[frame_size++] = frame.function_code;
    send_buffer[frame_size++] = frame.data_length;
    memcpy(&send_buffer[frame_size], frame.data, frame.data_length);
    frame_size += frame.data_length;
    send_buffer[frame_size++] = frame.fcs;
    
    if (write(fd, send_buffer, frame_size) != frame_size) {
        perror("Profibus write failed");
        return -1;
    }
    
    return 0;
}

// Main gateway process loop
void gateway_process(Gateway_Context* ctx) {
    printf("Starting EtherNet/IP to Profibus gateway...\n");
    
    while (ctx->running) {
        for (int i = 0; i < ctx->mapping_count; i++) {
            Gateway_Mapping* map = &ctx->mappings[i];
            uint16_t length;
            
            // Read from EtherNet/IP
            if (read_ethernet_ip_data(ctx->ethernet_ip_socket,
                                     map->ethernet_ip_instance,
                                     map->buffer,
                                     &length) == 0) {
                
                // Write to Profibus
                if (write_profibus_data(ctx->profibus_fd,
                                       map->profibus_slave_addr,
                                       map->profibus_offset,
                                       map->buffer,
                                       length) == 0) {
                    printf("Mapped %d bytes: EIP Instance %d -> PB Slave %d\n",
                           length, map->ethernet_ip_instance, 
                           map->profibus_slave_addr);
                }
            }
        }
        
        usleep(10000); // 10ms cycle time
    }
}

// Example usage
int main() {
    Gateway_Context gateway;
    gateway.mapping_count = 2;
    gateway.mappings = malloc(sizeof(Gateway_Mapping) * 2);
    gateway.running = 1;
    
    // Map EtherNet/IP instance 100 to Profibus slave 3, offset 0
    gateway.mappings[0] = *create_mapping(100, 3, 0, 32);
    
    // Map EtherNet/IP instance 101 to Profibus slave 4, offset 0
    gateway.mappings[1] = *create_mapping(101, 4, 0, 16);
    
    // Initialize connections (simplified)
    // gateway.ethernet_ip_socket = connect_ethernet_ip("192.168.1.10", 44818);
    // gateway.profibus_fd = open_profibus("/dev/profibus0");
    
    // gateway_process(&gateway);
    
    printf("Gateway configuration created successfully.\n");
    
    // Cleanup
    for (int i = 0; i < gateway.mapping_count; i++) {
        free(gateway.mappings[i].buffer);
    }
    free(gateway.mappings);
    
    return 0;
}
```

### Rust Example - Gateway Data Mapping Service

```rust
use std::io::{self, Read, Write};
use std::net::TcpStream;
use std::time::Duration;
use std::sync::{Arc, Mutex};
use std::thread;

// EtherNet/IP structures
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct CipRequest {
    service: u8,
    request_path_size: u8,
    class_id: u16,
    instance_id: u16,
    attribute_id: u16,
}

#[derive(Debug)]
struct CipResponse {
    service: u8,
    status: u8,
    data: Vec<u8>,
}

// Profibus DP structures
#[derive(Debug, Clone)]
struct ProfibusFrame {
    station_address: u8,
    function_code: u8,
    data: Vec<u8>,
}

impl ProfibusFrame {
    fn new(address: u8, function: u8, data: Vec<u8>) -> Self {
        ProfibusFrame {
            station_address: address,
            function_code: function,
            data,
        }
    }
    
    fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.station_address);
        bytes.push(self.function_code);
        bytes.push(self.data.len() as u8);
        bytes.extend_from_slice(&self.data);
        
        // Calculate FCS
        let fcs: u8 = bytes.iter().fold(0, |acc, &x| acc ^ x);
        bytes.push(fcs);
        
        bytes
    }
}

// Gateway mapping configuration
#[derive(Debug, Clone)]
struct GatewayMapping {
    name: String,
    ethernet_ip_instance: u16,
    profibus_slave_address: u8,
    profibus_offset: u16,
    data_size: usize,
}

impl GatewayMapping {
    fn new(name: &str, eip_inst: u16, pb_slave: u8, pb_offset: u16, size: usize) -> Self {
        GatewayMapping {
            name: name.to_string(),
            ethernet_ip_instance: eip_inst,
            profibus_slave_address: pb_slave,
            profibus_offset: pb_offset,
            data_size: size,
        }
    }
}

// EtherNet/IP client
struct EthernetIpClient {
    stream: TcpStream,
    session_handle: u32,
}

impl EthernetIpClient {
    fn connect(address: &str) -> io::Result<Self> {
        let stream = TcpStream::connect(address)?;
        stream.set_read_timeout(Some(Duration::from_secs(5)))?;
        stream.set_write_timeout(Some(Duration::from_secs(5)))?;
        
        println!("Connected to EtherNet/IP device at {}", address);
        
        Ok(EthernetIpClient {
            stream,
            session_handle: 0x12345678, // Would be obtained from RegisterSession
        })
    }
    
    fn read_assembly(&mut self, instance: u16) -> io::Result<Vec<u8>> {
        // Create CIP Get_Attribute_Single request
        let request = CipRequest {
            service: 0x0E, // Get_Attribute_Single
            request_path_size: 3,
            class_id: 0x04, // Assembly Object
            instance_id: instance,
            attribute_id: 0x03, // Data attribute
        };
        
        // Convert to bytes (simplified)
        let request_bytes = unsafe {
            std::slice::from_raw_parts(
                &request as *const _ as *const u8,
                std::mem::size_of::<CipRequest>()
            )
        };
        
        self.stream.write_all(request_bytes)?;
        
        // Read response
        let mut response_buffer = vec![0u8; 512];
        let bytes_read = self.stream.read(&mut response_buffer)?;
        
        if bytes_read >= 4 {
            let status = response_buffer[2];
            if status == 0 {
                // Success - return data portion
                let data_start = 6; // Skip header
                return Ok(response_buffer[data_start..bytes_read].to_vec());
            }
        }
        
        Err(io::Error::new(io::ErrorKind::Other, "CIP request failed"))
    }
}

// Profibus DP master interface
struct ProfibusInterface {
    port_name: String,
}

impl ProfibusInterface {
    fn new(port: &str) -> Self {
        ProfibusInterface {
            port_name: port.to_string(),
        }
    }
    
    fn write_slave_data(&self, slave_addr: u8, offset: u16, data: &[u8]) -> io::Result<()> {
        println!("Writing {} bytes to Profibus slave {} at offset {}", 
                 data.len(), slave_addr, offset);
        
        // Pack offset and data
        let mut frame_data = Vec::new();
        frame_data.push((offset >> 8) as u8);
        frame_data.push((offset & 0xFF) as u8);
        frame_data.extend_from_slice(data);
        
        // Create Profibus frame
        let frame = ProfibusFrame::new(
            slave_addr,
            0x5D, // SDN - Send Data with No acknowledge
            frame_data,
        );
        
        let frame_bytes = frame.to_bytes();
        
        // In a real implementation, this would write to the Profibus interface
        // For demonstration, we'll just log it
        println!("Profibus frame: {:02X?}", frame_bytes);
        
        Ok(())
    }
    
    fn read_slave_data(&self, slave_addr: u8, offset: u16, length: usize) -> io::Result<Vec<u8>> {
        println!("Reading {} bytes from Profibus slave {} at offset {}", 
                 length, slave_addr, offset);
        
        // Create read request frame
        let mut request_data = Vec::new();
        request_data.push((offset >> 8) as u8);
        request_data.push((offset & 0xFF) as u8);
        request_data.push(length as u8);
        
        let frame = ProfibusFrame::new(
            slave_addr,
            0x5C, // RDN - Request Data with No acknowledge
            request_data,
        );
        
        // In real implementation, send and receive
        Ok(vec![0u8; length]) // Dummy data
    }
}

// Gateway coordinator
struct Gateway {
    ethernet_ip_client: Arc<Mutex<EthernetIpClient>>,
    profibus_interface: Arc<ProfibusInterface>,
    mappings: Vec<GatewayMapping>,
}

impl Gateway {
    fn new(eip_address: &str, profibus_port: &str) -> io::Result<Self> {
        let eip_client = EthernetIpClient::connect(eip_address)?;
        let profibus = ProfibusInterface::new(profibus_port);
        
        Ok(Gateway {
            ethernet_ip_client: Arc::new(Mutex::new(eip_client)),
            profibus_interface: Arc::new(profibus),
            mappings: Vec::new(),
        })
    }
    
    fn add_mapping(&mut self, mapping: GatewayMapping) {
        println!("Added mapping: {}", mapping.name);
        self.mappings.push(mapping);
    }
    
    fn process_mapping(&self, mapping: &GatewayMapping) -> io::Result<()> {
        // Read from EtherNet/IP
        let data = {
            let mut client = self.ethernet_ip_client.lock().unwrap();
            client.read_assembly(mapping.ethernet_ip_instance)?
        };
        
        // Write to Profibus
        self.profibus_interface.write_slave_data(
            mapping.profibus_slave_address,
            mapping.profibus_offset,
            &data[..mapping.data_size.min(data.len())],
        )?;
        
        println!("Processed mapping '{}': {} bytes transferred", 
                 mapping.name, data.len());
        
        Ok(())
    }
    
    fn run(&self, cycle_time_ms: u64) {
        println!("Gateway started with {}ms cycle time", cycle_time_ms);
        
        loop {
            for mapping in &self.mappings {
                if let Err(e) = self.process_mapping(mapping) {
                    eprintln!("Error processing mapping '{}': {}", mapping.name, e);
                }
            }
            
            thread::sleep(Duration::from_millis(cycle_time_ms));
        }
    }
}

// Example usage
fn main() -> io::Result<()> {
    println!("EtherNet/IP to Profibus Gateway");
    println!("================================\n");
    
    // Create gateway
    let mut gateway = Gateway::new("192.168.1.10:44818", "/dev/profibus0")?;
    
    // Configure mappings
    gateway.add_mapping(GatewayMapping::new(
        "PLC1_OutputData",
        100,  // EtherNet/IP assembly instance
        3,    // Profibus slave address
        0,    // Profibus offset
        32,   // Data size in bytes
    ));
    
    gateway.add_mapping(GatewayMapping::new(
        "PLC1_InputData",
        101,
        3,
        32,
        16,
    ));
    
    gateway.add_mapping(GatewayMapping::new(
        "IO_Module_Data",
        102,
        4,
        0,
        8,
    ));
    
    println!("\nStarting gateway operation...\n");
    
    // Run gateway (would normally run indefinitely)
    // gateway.run(10); // 10ms cycle time
    
    println!("Gateway configured successfully.");
    
    Ok(())
}
```

## Summary

EtherNet/IP to Profibus gateways enable crucial interoperability between Allen-Bradley/Rockwell automation systems and Siemens Profibus networks. These gateways perform real-time protocol conversion, translating between Ethernet-based CIP messaging and RS-485 fieldbus telegrams.

Key implementation considerations include:
- **Bi-directional data mapping** between EtherNet/IP assembly objects and Profibus slave I/O
- **Cycle time synchronization** to ensure both networks operate efficiently
- **Error handling and diagnostics** for both protocol stacks
- **Configuration management** for defining data mappings between systems

The C/C++ example demonstrates low-level protocol handling with direct control over frame construction, while the Rust implementation shows a more structured, type-safe approach with better concurrency handling. Both examples illustrate the core gateway functions: reading from EtherNet/IP devices, translating data formats, and writing to Profibus slaves.

In production environments, commercial gateways often provide web-based configuration tools, automatic device discovery, and redundancy features. However, custom implementations may be necessary for specialized requirements or legacy system integration where standard gateways don't provide sufficient flexibility.