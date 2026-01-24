# Profibus Master Class 2 Implementation

## Detailed Description

### Overview
Profibus defines two classes of masters in the DP (Decentralized Periphery) protocol:
- **Class 1 Master (DPM1)**: Handles cyclic data exchange with slaves for real-time control
- **Class 2 Master (DPM2)**: Provides acyclic services for configuration, diagnostics, parameterization, and monitoring

A **Class 2 Master** is typically used by engineering tools, HMI systems, or diagnostic software. It doesn't participate in the cyclic control loop but can read/write parameters, request diagnostic data, and configure devices while the Class 1 Master maintains real-time operations.

### Key Characteristics

**Acyclic Communication**:
- Non-time-critical operations
- Request/response based
- Can access any slave on the bus
- Doesn't interfere with cyclic Class 1 traffic

**Primary Functions**:
1. **Configuration**: Download parameters to slaves
2. **Diagnostics**: Read device status and diagnostic information
3. **Monitoring**: Observe process data without participating in control
4. **Identification**: Read manufacturer and device information
5. **Parameter Management**: Read/write slave parameters

**Token Handling**:
- Class 2 Masters participate in the token ring
- Lower priority than Class 1 Masters
- Must yield token quickly to maintain real-time performance

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Profibus DP service codes
#define PROFIBUS_READ_SLAVE_DIAG    0x3C
#define PROFIBUS_READ_INPUTS        0x3D
#define PROFIBUS_READ_OUTPUTS       0x3E
#define PROFIBUS_SET_SLAVE_PARAM    0x3F
#define PROFIBUS_GET_SLAVE_PARAM    0x40

// Diagnostic status flags
#define DIAG_STAT_MASTER_LOCK      0x01
#define DIAG_STAT_PARAM_FAULT      0x02
#define DIAG_STAT_INVALID_CONFIG   0x04
#define DIAG_STAT_EXT_DIAG         0x08
#define DIAG_STAT_NOT_READY        0x10
#define DIAG_STAT_CFG_FAULT        0x20
#define DIAG_STAT_STATION_NON_EXISTENT 0x40
#define DIAG_STAT_STATION_NOT_READY    0x80

// Class 2 Master structure
typedef struct {
    uint8_t master_address;
    uint8_t *token_ring;
    size_t token_ring_size;
    bool has_token;
    uint32_t timeout_ms;
} profibus_class2_master_t;

// Diagnostic data structure
typedef struct {
    uint8_t station_status_1;
    uint8_t station_status_2;
    uint8_t station_status_3;
    uint8_t master_address;
    uint16_t ident_number;
    uint8_t ext_diag_length;
    uint8_t ext_diag_data[244];
} profibus_diag_data_t;

// Parameter data structure
typedef struct {
    uint8_t station_address;
    uint8_t *param_data;
    uint16_t param_length;
} profibus_param_t;

// Initialize Class 2 Master
bool profibus_class2_init(profibus_class2_master_t *master, 
                          uint8_t address) {
    if (!master || address > 126) {
        return false;
    }
    
    master->master_address = address;
    master->has_token = false;
    master->timeout_ms = 1000;
    master->token_ring = NULL;
    master->token_ring_size = 0;
    
    return true;
}

// Read slave diagnostics
int profibus_class2_read_diagnostics(profibus_class2_master_t *master,
                                     uint8_t slave_address,
                                     profibus_diag_data_t *diag) {
    if (!master || !diag || slave_address > 126) {
        return -1;
    }
    
    // Wait for token
    while (!master->has_token) {
        // Token handling logic would go here
    }
    
    // Build diagnostic request telegram
    uint8_t telegram[256];
    size_t pos = 0;
    
    telegram[pos++] = 0x68; // Start delimiter (long frame)
    telegram[pos++] = 0;    // Length (filled later)
    telegram[pos++] = 0;    // Length repeated
    telegram[pos++] = 0x68; // Start delimiter repeated
    telegram[pos++] = slave_address; // Destination
    telegram[pos++] = master->master_address; // Source
    telegram[pos++] = PROFIBUS_READ_SLAVE_DIAG; // Function code
    
    // Set length
    uint8_t length = pos - 4;
    telegram[1] = length;
    telegram[2] = length;
    
    // Add FCS (Frame Check Sequence)
    uint8_t fcs = 0;
    for (size_t i = 4; i < pos; i++) {
        fcs += telegram[i];
    }
    telegram[pos++] = fcs;
    telegram[pos++] = 0x16; // End delimiter
    
    // Send telegram (hardware-specific)
    // send_profibus_telegram(telegram, pos);
    
    // Receive response (hardware-specific)
    uint8_t response[256];
    // int rx_len = receive_profibus_telegram(response, sizeof(response), 
    //                                        master->timeout_ms);
    
    // Parse diagnostic response (simplified)
    int rx_len = 20; // Example
    if (rx_len < 6) {
        return -2; // Invalid response
    }
    
    // Extract diagnostic data
    diag->station_status_1 = response[7];
    diag->station_status_2 = response[8];
    diag->station_status_3 = response[9];
    diag->master_address = response[10];
    diag->ident_number = (response[11] << 8) | response[12];
    
    if (rx_len > 13) {
        diag->ext_diag_length = response[13];
        size_t copy_len = (diag->ext_diag_length < sizeof(diag->ext_diag_data)) 
                          ? diag->ext_diag_length 
                          : sizeof(diag->ext_diag_data);
        memcpy(diag->ext_diag_data, &response[14], copy_len);
    }
    
    // Pass token to next master
    master->has_token = false;
    
    return 0;
}

// Check if slave has diagnostic issues
bool profibus_class2_has_diagnostics(const profibus_diag_data_t *diag) {
    return (diag->station_status_1 & DIAG_STAT_EXT_DIAG) != 0;
}

// Read slave parameters
int profibus_class2_read_parameters(profibus_class2_master_t *master,
                                    uint8_t slave_address,
                                    uint8_t *param_buffer,
                                    uint16_t buffer_size,
                                    uint16_t *actual_size) {
    if (!master || !param_buffer || !actual_size) {
        return -1;
    }
    
    while (!master->has_token) {
        // Wait for token
    }
    
    // Build parameter read request
    uint8_t telegram[256];
    size_t pos = 0;
    
    telegram[pos++] = 0x68;
    telegram[pos++] = 3;  // Length
    telegram[pos++] = 3;
    telegram[pos++] = 0x68;
    telegram[pos++] = slave_address;
    telegram[pos++] = master->master_address;
    telegram[pos++] = PROFIBUS_GET_SLAVE_PARAM;
    
    uint8_t fcs = slave_address + master->master_address + 
                  PROFIBUS_GET_SLAVE_PARAM;
    telegram[pos++] = fcs;
    telegram[pos++] = 0x16;
    
    // Send and receive (hardware-specific implementation)
    // send_profibus_telegram(telegram, pos);
    // int rx_len = receive_profibus_telegram(param_buffer, buffer_size, 
    //                                        master->timeout_ms);
    
    *actual_size = 10; // Example
    master->has_token = false;
    
    return 0;
}

// Monitor slave I/O data (non-intrusive)
int profibus_class2_monitor_io(profibus_class2_master_t *master,
                               uint8_t slave_address,
                               uint8_t *input_data,
                               uint8_t *output_data,
                               uint8_t *io_length) {
    if (!master || !input_data || !output_data || !io_length) {
        return -1;
    }
    
    while (!master->has_token) {
        // Wait for token
    }
    
    // Read inputs
    uint8_t telegram[256];
    size_t pos = 0;
    
    telegram[pos++] = 0x68;
    telegram[pos++] = 3;
    telegram[pos++] = 3;
    telegram[pos++] = 0x68;
    telegram[pos++] = slave_address;
    telegram[pos++] = master->master_address;
    telegram[pos++] = PROFIBUS_READ_INPUTS;
    
    uint8_t fcs = slave_address + master->master_address + 
                  PROFIBUS_READ_INPUTS;
    telegram[pos++] = fcs;
    telegram[pos++] = 0x16;
    
    // Send/receive implementation
    *io_length = 4; // Example
    
    master->has_token = false;
    return 0;
}

// Print diagnostic information
void profibus_class2_print_diagnostics(const profibus_diag_data_t *diag) {
    printf("Diagnostic Information:\n");
    printf("  Status 1: 0x%02X\n", diag->station_status_1);
    
    if (diag->station_status_1 & DIAG_STAT_MASTER_LOCK)
        printf("    - Master lock active\n");
    if (diag->station_status_1 & DIAG_STAT_PARAM_FAULT)
        printf("    - Parameter fault\n");
    if (diag->station_status_1 & DIAG_STAT_INVALID_CONFIG)
        printf("    - Invalid configuration\n");
    if (diag->station_status_1 & DIAG_STAT_EXT_DIAG)
        printf("    - Extended diagnostics available\n");
    
    printf("  Master Address: %d\n", diag->master_address);
    printf("  Ident Number: 0x%04X\n", diag->ident_number);
    
    if (diag->ext_diag_length > 0) {
        printf("  Extended Diagnostics (%d bytes):\n", diag->ext_diag_length);
        for (int i = 0; i < diag->ext_diag_length && i < 16; i++) {
            printf("    [%d]: 0x%02X\n", i, diag->ext_diag_data[i]);
        }
    }
}
```

### Rust Implementation

```rust
use std::time::Duration;
use std::collections::HashMap;

// Profibus service codes
const PROFIBUS_READ_SLAVE_DIAG: u8 = 0x3C;
const PROFIBUS_READ_INPUTS: u8 = 0x3D;
const PROFIBUS_READ_OUTPUTS: u8 = 0x3E;
const PROFIBUS_SET_SLAVE_PARAM: u8 = 0x3F;
const PROFIBUS_GET_SLAVE_PARAM: u8 = 0x40;

// Diagnostic status flags
const DIAG_STAT_MASTER_LOCK: u8 = 0x01;
const DIAG_STAT_PARAM_FAULT: u8 = 0x02;
const DIAG_STAT_INVALID_CONFIG: u8 = 0x04;
const DIAG_STAT_EXT_DIAG: u8 = 0x08;
const DIAG_STAT_NOT_READY: u8 = 0x10;

#[derive(Debug, Clone)]
pub struct DiagnosticData {
    pub station_status_1: u8,
    pub station_status_2: u8,
    pub station_status_3: u8,
    pub master_address: u8,
    pub ident_number: u16,
    pub ext_diag_data: Vec<u8>,
}

impl DiagnosticData {
    pub fn has_extended_diagnostics(&self) -> bool {
        (self.station_status_1 & DIAG_STAT_EXT_DIAG) != 0
    }
    
    pub fn has_parameter_fault(&self) -> bool {
        (self.station_status_1 & DIAG_STAT_PARAM_FAULT) != 0
    }
    
    pub fn is_station_ready(&self) -> bool {
        (self.station_status_1 & DIAG_STAT_NOT_READY) == 0
    }
    
    pub fn print_status(&self) {
        println!("Diagnostic Information:");
        println!("  Status 1: 0x{:02X}", self.station_status_1);
        
        if self.station_status_1 & DIAG_STAT_MASTER_LOCK != 0 {
            println!("    - Master lock active");
        }
        if self.station_status_1 & DIAG_STAT_PARAM_FAULT != 0 {
            println!("    - Parameter fault");
        }
        if self.station_status_1 & DIAG_STAT_INVALID_CONFIG != 0 {
            println!("    - Invalid configuration");
        }
        if self.has_extended_diagnostics() {
            println!("    - Extended diagnostics available");
        }
        
        println!("  Master Address: {}", self.master_address);
        println!("  Ident Number: 0x{:04X}", self.ident_number);
        
        if !self.ext_diag_data.is_empty() {
            println!("  Extended Diagnostics ({} bytes):", 
                     self.ext_diag_data.len());
            for (i, byte) in self.ext_diag_data.iter().take(16).enumerate() {
                println!("    [{}]: 0x{:02X}", i, byte);
            }
        }
    }
}

#[derive(Debug)]
pub enum ProfibusError {
    InvalidAddress,
    Timeout,
    InvalidResponse,
    CommunicationError(String),
    NoToken,
}

pub struct Class2Master {
    master_address: u8,
    token_ring: Vec<u8>,
    has_token: bool,
    timeout: Duration,
    slave_diagnostics: HashMap<u8, DiagnosticData>,
}

impl Class2Master {
    pub fn new(master_address: u8) -> Result<Self, ProfibusError> {
        if master_address > 126 {
            return Err(ProfibusError::InvalidAddress);
        }
        
        Ok(Class2Master {
            master_address,
            token_ring: Vec::new(),
            has_token: false,
            timeout: Duration::from_millis(1000),
            slave_diagnostics: HashMap::new(),
        })
    }
    
    pub fn set_timeout(&mut self, timeout: Duration) {
        self.timeout = timeout;
    }
    
    fn build_telegram(&self, slave_address: u8, function_code: u8, 
                      data: &[u8]) -> Vec<u8> {
        let mut telegram = Vec::new();
        
        telegram.push(0x68); // Start delimiter
        let length = 3 + data.len() as u8;
        telegram.push(length);
        telegram.push(length);
        telegram.push(0x68); // Start delimiter repeated
        telegram.push(slave_address);
        telegram.push(self.master_address);
        telegram.push(function_code);
        telegram.extend_from_slice(data);
        
        // Calculate FCS
        let fcs: u8 = telegram[4..]
            .iter()
            .fold(0u8, |acc, &x| acc.wrapping_add(x));
        telegram.push(fcs);
        telegram.push(0x16); // End delimiter
        
        telegram
    }
    
    fn wait_for_token(&mut self) -> Result<(), ProfibusError> {
        // In a real implementation, this would wait for the token
        // from the token ring protocol
        if !self.has_token {
            // Simulated token wait
            std::thread::sleep(Duration::from_millis(10));
            self.has_token = true;
        }
        Ok(())
    }
    
    fn release_token(&mut self) {
        self.has_token = false;
    }
    
    pub fn read_diagnostics(&mut self, slave_address: u8) 
        -> Result<DiagnosticData, ProfibusError> {
        if slave_address > 126 {
            return Err(ProfibusError::InvalidAddress);
        }
        
        self.wait_for_token()?;
        
        // Build diagnostic request
        let telegram = self.build_telegram(
            slave_address, 
            PROFIBUS_READ_SLAVE_DIAG, 
            &[]
        );
        
        // Send telegram (hardware-specific)
        // self.send_telegram(&telegram)?;
        
        // Receive response (hardware-specific)
        // let response = self.receive_telegram()?;
        
        // Simulated response parsing
        let response = vec![
            0x68, 0x0E, 0x0E, 0x68, 
            slave_address, self.master_address,
            0x08, // Status
            0x00, // Station status 1
            0x00, // Station status 2
            0x00, // Station status 3
            self.master_address, // Master address
            0x12, 0x34, // Ident number
            0x00, // Ext diag length
            0x00, 0x16
        ];
        
        let diag = self.parse_diagnostic_response(&response)?;
        
        // Cache diagnostics
        self.slave_diagnostics.insert(slave_address, diag.clone());
        
        self.release_token();
        Ok(diag)
    }
    
    fn parse_diagnostic_response(&self, response: &[u8]) 
        -> Result<DiagnosticData, ProfibusError> {
        if response.len() < 14 {
            return Err(ProfibusError::InvalidResponse);
        }
        
        let station_status_1 = response[7];
        let station_status_2 = response[8];
        let station_status_3 = response[9];
        let master_address = response[10];
        let ident_number = ((response[11] as u16) << 8) | (response[12] as u16);
        
        let ext_diag_data = if response.len() > 14 {
            let ext_diag_length = response[13] as usize;
            response[14..14 + ext_diag_length.min(response.len() - 14)]
                .to_vec()
        } else {
            Vec::new()
        };
        
        Ok(DiagnosticData {
            station_status_1,
            station_status_2,
            station_status_3,
            master_address,
            ident_number,
            ext_diag_data,
        })
    }
    
    pub fn read_parameters(&mut self, slave_address: u8) 
        -> Result<Vec<u8>, ProfibusError> {
        if slave_address > 126 {
            return Err(ProfibusError::InvalidAddress);
        }
        
        self.wait_for_token()?;
        
        let telegram = self.build_telegram(
            slave_address,
            PROFIBUS_GET_SLAVE_PARAM,
            &[]
        );
        
        // Send/receive implementation would go here
        let params = vec![0x00, 0x01, 0x02, 0x03]; // Example
        
        self.release_token();
        Ok(params)
    }
    
    pub fn write_parameters(&mut self, slave_address: u8, params: &[u8]) 
        -> Result<(), ProfibusError> {
        if slave_address > 126 {
            return Err(ProfibusError::InvalidAddress);
        }
        
        self.wait_for_token()?;
        
        let telegram = self.build_telegram(
            slave_address,
            PROFIBUS_SET_SLAVE_PARAM,
            params
        );
        
        // Send telegram and wait for acknowledgment
        
        self.release_token();
        Ok(())
    }
    
    pub fn monitor_inputs(&mut self, slave_address: u8) 
        -> Result<Vec<u8>, ProfibusError> {
        self.wait_for_token()?;
        
        let telegram = self.build_telegram(
            slave_address,
            PROFIBUS_READ_INPUTS,
            &[]
        );
        
        // Send/receive implementation
        let inputs = vec![0xFF, 0x00, 0xAA, 0x55]; // Example
        
        self.release_token();
        Ok(inputs)
    }
    
    pub fn monitor_outputs(&mut self, slave_address: u8) 
        -> Result<Vec<u8>, ProfibusError> {
        self.wait_for_token()?;
        
        let telegram = self.build_telegram(
            slave_address,
            PROFIBUS_READ_OUTPUTS,
            &[]
        );
        
        // Send/receive implementation
        let outputs = vec![0x00, 0xFF, 0x55, 0xAA]; // Example
        
        self.release_token();
        Ok(outputs)
    }
    
    pub fn get_cached_diagnostics(&self, slave_address: u8) 
        -> Option<&DiagnosticData> {
        self.slave_diagnostics.get(&slave_address)
    }
    
    pub fn scan_network(&mut self) -> Result<Vec<u8>, ProfibusError> {
        let mut active_slaves = Vec::new();
        
        for address in 0..=126 {
            if let Ok(_diag) = self.read_diagnostics(address) {
                active_slaves.push(address);
            }
        }
        
        Ok(active_slaves)
    }
}

// Example usage
fn main() -> Result<(), ProfibusError> {
    let mut master = Class2Master::new(1)?;
    master.set_timeout(Duration::from_millis(500));
    
    println!("Class 2 Master initialized at address {}", 1);
    
    // Read diagnostics from slave 5
    match master.read_diagnostics(5) {
        Ok(diag) => {
            diag.print_status();
            
            if diag.is_station_ready() {
                println!("Slave 5 is ready");
            } else {
                println!("Slave 5 is NOT ready");
            }
        }
        Err(e) => println!("Failed to read diagnostics: {:?}", e),
    }
    
    // Monitor I/O data
    match master.monitor_inputs(5) {
        Ok(inputs) => println!("Input data: {:02X?}", inputs),
        Err(e) => println!("Failed to read inputs: {:?}", e),
    }
    
    // Read parameters
    match master.read_parameters(5) {
        Ok(params) => println!("Parameters: {:02X?}", params),
        Err(e) => println!("Failed to read parameters: {:?}", e),
    }
    
    Ok(())
}
```

## Summary

**Profibus Master Class 2 Implementation** provides essential engineering and diagnostic capabilities for Profibus DP networks:

**Core Capabilities**:
- **Acyclic Communication**: Non-real-time request/response services that don't interfere with Class 1 cyclic control
- **Diagnostic Access**: Read comprehensive device status, extended diagnostics, and fault information
- **Parameter Management**: Read and write device configuration parameters during operation
- **I/O Monitoring**: Non-intrusive observation of process data for debugging and analysis
- **Network Scanning**: Identify and interrogate all devices on the network

**Key Differences from Class 1**:
- Class 1 focuses on deterministic cyclic data exchange; Class 2 provides engineering access
- Class 2 has lower token priority to avoid disrupting real-time traffic
- Multiple Class 2 Masters can coexist for different tools (HMI, diagnostics, configuration)

**Typical Use Cases**:
- Engineering workstations for device configuration
- HMI systems for parameter adjustment and monitoring
- Diagnostic tools for troubleshooting and maintenance
- Asset management systems for device identification and inventory

The implementation shown demonstrates telegram construction, diagnostic parsing, parameter access, and proper token ring participation, providing a foundation for building comprehensive Profibus engineering and diagnostic tools.