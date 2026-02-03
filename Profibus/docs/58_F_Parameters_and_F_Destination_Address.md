# F-Parameters and F-Destination Address in Profibus

## Detailed Description

### Overview
F-Parameters (Fail-safe Parameters) and F-Destination Address are critical components of PROFIsafe (PROFIBUS Safety), which is a safety protocol extension that runs on top of standard PROFIBUS to enable Safety Integrity Level (SIL) 3 certified communication for safety-critical applications.

### What are F-Parameters?

F-Parameters are safety-specific configuration parameters used to establish and maintain safe communication between PROFIsafe devices. These parameters ensure that the safety protocol operates correctly and can detect communication errors, device failures, and data corruption.

**Key F-Parameters include:**

1. **F_Source_Address**: The address of the safety controller (F-host)
2. **F_Destination_Address**: The unique address of the safety device (F-device)
3. **F_WD_Time**: Watchdog timeout for monitoring communication
4. **F_CRC_Length**: Length of the CRC for data integrity (2, 3, or 4 bytes)
5. **F_Par_Version**: Parameter version for consistency checking
6. **F_SIL**: Required Safety Integrity Level (SIL 1, 2, or 3)
7. **F_iPar_CRC**: CRC over all F-Parameters to ensure parameter integrity

### F-Destination Address

The F-Destination Address is a unique identifier (typically 16-bit) assigned to each PROFIsafe device in the network. Unlike the standard PROFIBUS address, the F-Destination Address:

- Must be globally unique within the F-system
- Remains constant even if the device's physical PROFIBUS address changes
- Is used to prevent device mix-ups and ensure messages reach the correct safety device
- Works in conjunction with F-Source Address to create a unique communication relationship

### Safety Mechanism

PROFIsafe implements several safety mechanisms:

- **Sequence numbering**: Detects loss, repetition, or incorrect sequence of telegrams
- **Timeout monitoring**: Ensures timely communication
- **Data integrity**: CRC checking on safety data
- **Source/Destination checking**: Validates communication endpoints
- **Parameter monitoring**: Ensures configuration consistency

## Programming Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// PROFIsafe Protocol Constants
#define F_CRC_LENGTH_2BYTE  2
#define F_CRC_LENGTH_3BYTE  3
#define F_CRC_LENGTH_4BYTE  4

#define F_SIL_1  1
#define F_SIL_2  2
#define F_SIL_3  3

// F-Parameter Structure
typedef struct {
    uint16_t f_source_addr;        // F-Source Address
    uint16_t f_dest_addr;          // F-Destination Address
    uint32_t f_wd_time;            // Watchdog time in ms
    uint8_t  f_crc_length;         // CRC length (2, 3, or 4 bytes)
    uint8_t  f_par_version;        // Parameter version
    uint8_t  f_sil;                // Safety Integrity Level
    uint32_t f_ipar_crc;           // CRC over F-Parameters
} FParameters;

// PROFIsafe Message Structure
typedef struct {
    uint8_t  status_control;       // Status/Control byte
    uint8_t  toggle_bit;           // Toggle bit for sequence detection
    uint8_t  data[256];            // Safety data payload
    uint16_t data_length;          // Length of safety data
    uint32_t crc;                  // Safety CRC
} FSafeMessage;

// CRC calculation for PROFIsafe (simplified CRC-32)
uint32_t calculate_profisafe_crc(const uint8_t *data, uint16_t length, 
                                  uint16_t f_source, uint16_t f_dest) {
    uint32_t crc = 0xFFFFFFFF;
    
    // Include F-addresses in CRC calculation
    uint8_t addr_bytes[4];
    addr_bytes[0] = (f_source >> 8) & 0xFF;
    addr_bytes[1] = f_source & 0xFF;
    addr_bytes[2] = (f_dest >> 8) & 0xFF;
    addr_bytes[3] = f_dest & 0xFF;
    
    // Calculate CRC over addresses
    for (int i = 0; i < 4; i++) {
        crc ^= addr_bytes[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    // Calculate CRC over data
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return ~crc;
}

// Calculate CRC over F-Parameters for integrity check
uint32_t calculate_fparameter_crc(const FParameters *params) {
    uint8_t buffer[16];
    int offset = 0;
    
    buffer[offset++] = (params->f_source_addr >> 8) & 0xFF;
    buffer[offset++] = params->f_source_addr & 0xFF;
    buffer[offset++] = (params->f_dest_addr >> 8) & 0xFF;
    buffer[offset++] = params->f_dest_addr & 0xFF;
    buffer[offset++] = (params->f_wd_time >> 24) & 0xFF;
    buffer[offset++] = (params->f_wd_time >> 16) & 0xFF;
    buffer[offset++] = (params->f_wd_time >> 8) & 0xFF;
    buffer[offset++] = params->f_wd_time & 0xFF;
    buffer[offset++] = params->f_crc_length;
    buffer[offset++] = params->f_par_version;
    buffer[offset++] = params->f_sil;
    
    return calculate_profisafe_crc(buffer, offset, 0, 0);
}

// Initialize F-Parameters
bool init_fparameters(FParameters *params, 
                      uint16_t source_addr,
                      uint16_t dest_addr,
                      uint32_t watchdog_ms,
                      uint8_t sil_level) {
    if (!params || sil_level < F_SIL_1 || sil_level > F_SIL_3) {
        return false;
    }
    
    params->f_source_addr = source_addr;
    params->f_dest_addr = dest_addr;
    params->f_wd_time = watchdog_ms;
    params->f_crc_length = F_CRC_LENGTH_4BYTE; // Use 4-byte CRC for highest safety
    params->f_par_version = 1;
    params->f_sil = sil_level;
    
    // Calculate and store parameter CRC
    params->f_ipar_crc = calculate_fparameter_crc(params);
    
    return true;
}

// Validate F-Parameters
bool validate_fparameters(const FParameters *params) {
    if (!params) {
        return false;
    }
    
    // Calculate expected CRC
    FParameters temp = *params;
    uint32_t expected_crc = calculate_fparameter_crc(&temp);
    
    // Verify CRC matches
    if (params->f_ipar_crc != expected_crc) {
        return false;
    }
    
    // Validate parameter ranges
    if (params->f_sil < F_SIL_1 || params->f_sil > F_SIL_3) {
        return false;
    }
    
    if (params->f_crc_length != F_CRC_LENGTH_2BYTE &&
        params->f_crc_length != F_CRC_LENGTH_3BYTE &&
        params->f_crc_length != F_CRC_LENGTH_4BYTE) {
        return false;
    }
    
    return true;
}

// Build a PROFIsafe message
bool build_fsafe_message(FSafeMessage *msg,
                         const FParameters *params,
                         const uint8_t *safety_data,
                         uint16_t data_len,
                         uint8_t toggle_bit) {
    if (!msg || !params || !safety_data || data_len > 256) {
        return false;
    }
    
    // Set control byte (simplified - actual implementation more complex)
    msg->status_control = 0x00;
    msg->toggle_bit = toggle_bit & 0x01;
    
    // Copy safety data
    memcpy(msg->data, safety_data, data_len);
    msg->data_length = data_len;
    
    // Calculate CRC over data including toggle bit
    uint8_t crc_data[257];
    crc_data[0] = msg->toggle_bit;
    memcpy(&crc_data[1], safety_data, data_len);
    
    msg->crc = calculate_profisafe_crc(crc_data, data_len + 1,
                                       params->f_source_addr,
                                       params->f_dest_addr);
    
    return true;
}

// Verify received PROFIsafe message
bool verify_fsafe_message(const FSafeMessage *msg,
                          const FParameters *params) {
    if (!msg || !params) {
        return false;
    }
    
    // Recalculate CRC
    uint8_t crc_data[257];
    crc_data[0] = msg->toggle_bit;
    memcpy(&crc_data[1], msg->data, msg->data_length);
    
    uint32_t calculated_crc = calculate_profisafe_crc(crc_data, 
                                                       msg->data_length + 1,
                                                       params->f_source_addr,
                                                       params->f_dest_addr);
    
    return (msg->crc == calculated_crc);
}

// Example usage
int main() {
    FParameters f_params;
    FSafeMessage tx_msg, rx_msg;
    
    // Initialize F-Parameters for a safety device
    // F-Source: 0x0001 (Safety controller)
    // F-Dest: 0x0100 (Safety device)
    // Watchdog: 150ms
    // SIL 3
    if (!init_fparameters(&f_params, 0x0001, 0x0100, 150, F_SIL_3)) {
        return -1;
    }
    
    // Validate parameters
    if (!validate_fparameters(&f_params)) {
        return -1;
    }
    
    // Prepare safety data
    uint8_t safety_data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t toggle = 0;
    
    // Build PROFIsafe message
    if (!build_fsafe_message(&tx_msg, &f_params, safety_data, 
                             sizeof(safety_data), toggle)) {
        return -1;
    }
    
    // Simulate reception (in real system, would receive from network)
    memcpy(&rx_msg, &tx_msg, sizeof(FSafeMessage));
    
    // Verify received message
    if (verify_fsafe_message(&rx_msg, &f_params)) {
        // Message valid - process safety data
        // ...
    } else {
        // Message invalid - enter safe state
        // ...
    }
    
    return 0;
}
```

### RUST Implementation

```rust
use std::error::Error;
use std::fmt;

// PROFIsafe Constants
const F_CRC_LENGTH_2BYTE: u8 = 2;
const F_CRC_LENGTH_3BYTE: u8 = 3;
const F_CRC_LENGTH_4BYTE: u8 = 4;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SafetyIntegrityLevel {
    Sil1 = 1,
    Sil2 = 2,
    Sil3 = 3,
}

#[derive(Debug)]
pub enum ProfisafeError {
    InvalidParameter(String),
    CrcMismatch,
    InvalidSil,
    DataTooLarge,
}

impl fmt::Display for ProfisafeError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ProfisafeError::InvalidParameter(msg) => write!(f, "Invalid parameter: {}", msg),
            ProfisafeError::CrcMismatch => write!(f, "CRC mismatch"),
            ProfisafeError::InvalidSil => write!(f, "Invalid SIL level"),
            ProfisafeError::DataTooLarge => write!(f, "Data too large"),
        }
    }
}

impl Error for ProfisafeError {}

// F-Parameter Structure
#[derive(Debug, Clone)]
pub struct FParameters {
    pub f_source_addr: u16,
    pub f_dest_addr: u16,
    pub f_wd_time: u32,        // Watchdog time in milliseconds
    pub f_crc_length: u8,
    pub f_par_version: u8,
    pub f_sil: SafetyIntegrityLevel,
    pub f_ipar_crc: u32,
}

impl FParameters {
    /// Create new F-Parameters with validation
    pub fn new(
        source_addr: u16,
        dest_addr: u16,
        watchdog_ms: u32,
        sil: SafetyIntegrityLevel,
    ) -> Result<Self, ProfisafeError> {
        let mut params = FParameters {
            f_source_addr: source_addr,
            f_dest_addr: dest_addr,
            f_wd_time: watchdog_ms,
            f_crc_length: F_CRC_LENGTH_4BYTE,
            f_par_version: 1,
            f_sil: sil,
            f_ipar_crc: 0,
        };
        
        // Calculate parameter CRC
        params.f_ipar_crc = params.calculate_parameter_crc();
        
        Ok(params)
    }
    
    /// Calculate CRC over F-Parameters
    fn calculate_parameter_crc(&self) -> u32 {
        let mut buffer = Vec::new();
        
        buffer.extend_from_slice(&self.f_source_addr.to_be_bytes());
        buffer.extend_from_slice(&self.f_dest_addr.to_be_bytes());
        buffer.extend_from_slice(&self.f_wd_time.to_be_bytes());
        buffer.push(self.f_crc_length);
        buffer.push(self.f_par_version);
        buffer.push(self.f_sil as u8);
        
        calculate_crc32(&buffer, 0, 0)
    }
    
    /// Validate F-Parameters
    pub fn validate(&self) -> Result<(), ProfisafeError> {
        // Verify parameter CRC
        let expected_crc = self.calculate_parameter_crc();
        if self.f_ipar_crc != expected_crc {
            return Err(ProfisafeError::CrcMismatch);
        }
        
        // Validate CRC length
        if ![F_CRC_LENGTH_2BYTE, F_CRC_LENGTH_3BYTE, F_CRC_LENGTH_4BYTE]
            .contains(&self.f_crc_length) {
            return Err(ProfisafeError::InvalidParameter(
                "Invalid CRC length".to_string()
            ));
        }
        
        Ok(())
    }
}

// PROFIsafe Message Structure
#[derive(Debug, Clone)]
pub struct FSafeMessage {
    pub status_control: u8,
    pub toggle_bit: bool,
    pub data: Vec<u8>,
    pub crc: u32,
}

impl FSafeMessage {
    /// Build a new PROFIsafe message
    pub fn build(
        params: &FParameters,
        safety_data: &[u8],
        toggle_bit: bool,
    ) -> Result<Self, ProfisafeError> {
        if safety_data.len() > 256 {
            return Err(ProfisafeError::DataTooLarge);
        }
        
        // Prepare data for CRC calculation
        let mut crc_data = Vec::new();
        crc_data.push(toggle_bit as u8);
        crc_data.extend_from_slice(safety_data);
        
        let crc = calculate_crc32(
            &crc_data,
            params.f_source_addr,
            params.f_dest_addr,
        );
        
        Ok(FSafeMessage {
            status_control: 0x00,
            toggle_bit,
            data: safety_data.to_vec(),
            crc,
        })
    }
    
    /// Verify received PROFIsafe message
    pub fn verify(&self, params: &FParameters) -> Result<(), ProfisafeError> {
        // Recalculate CRC
        let mut crc_data = Vec::new();
        crc_data.push(self.toggle_bit as u8);
        crc_data.extend_from_slice(&self.data);
        
        let calculated_crc = calculate_crc32(
            &crc_data,
            params.f_source_addr,
            params.f_dest_addr,
        );
        
        if self.crc != calculated_crc {
            return Err(ProfisafeError::CrcMismatch);
        }
        
        Ok(())
    }
    
    /// Encode message to byte array for transmission
    pub fn encode(&self) -> Vec<u8> {
        let mut buffer = Vec::new();
        buffer.push(self.status_control);
        buffer.push(self.toggle_bit as u8);
        buffer.extend_from_slice(&self.data);
        buffer.extend_from_slice(&self.crc.to_le_bytes());
        buffer
    }
    
    /// Decode message from byte array
    pub fn decode(data: &[u8]) -> Result<Self, ProfisafeError> {
        if data.len() < 6 {
            return Err(ProfisafeError::InvalidParameter(
                "Data too short".to_string()
            ));
        }
        
        let status_control = data[0];
        let toggle_bit = data[1] != 0;
        let data_len = data.len() - 6; // Minus header and CRC
        let payload = data[2..2 + data_len].to_vec();
        
        let crc = u32::from_le_bytes([
            data[data.len() - 4],
            data[data.len() - 3],
            data[data.len() - 2],
            data[data.len() - 1],
        ]);
        
        Ok(FSafeMessage {
            status_control,
            toggle_bit,
            data: payload,
            crc,
        })
    }
}

/// Calculate CRC-32 for PROFIsafe
fn calculate_crc32(data: &[u8], f_source: u16, f_dest: u16) -> u32 {
    let mut crc: u32 = 0xFFFFFFFF;
    
    // Include F-addresses in CRC
    let addr_bytes = [
        (f_source >> 8) as u8,
        f_source as u8,
        (f_dest >> 8) as u8,
        f_dest as u8,
    ];
    
    // Process address bytes
    for &byte in &addr_bytes {
        crc ^= byte as u32;
        for _ in 0..8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    // Process data bytes
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    !crc
}

/// PROFIsafe Device Manager
pub struct ProfisafeDevice {
    params: FParameters,
    toggle_state: bool,
    watchdog_counter: u32,
}

impl ProfisafeDevice {
    pub fn new(params: FParameters) -> Result<Self, ProfisafeError> {
        params.validate()?;
        
        Ok(ProfisafeDevice {
            params,
            toggle_state: false,
            watchdog_counter: 0,
        })
    }
    
    /// Send safety data
    pub fn send_data(&mut self, data: &[u8]) -> Result<FSafeMessage, ProfisafeError> {
        let message = FSafeMessage::build(&self.params, data, self.toggle_state)?;
        
        // Toggle bit for next message
        self.toggle_state = !self.toggle_state;
        
        // Reset watchdog
        self.watchdog_counter = 0;
        
        Ok(message)
    }
    
    /// Receive and verify safety data
    pub fn receive_data(&mut self, message: &FSafeMessage) -> Result<Vec<u8>, ProfisafeError> {
        // Verify message integrity
        message.verify(&self.params)?;
        
        // Reset watchdog
        self.watchdog_counter = 0;
        
        Ok(message.data.clone())
    }
    
    /// Check watchdog timeout
    pub fn check_watchdog(&mut self, elapsed_ms: u32) -> bool {
        self.watchdog_counter += elapsed_ms;
        self.watchdog_counter < self.params.f_wd_time
    }
}

// Example usage
fn main() -> Result<(), Box<dyn Error>> {
    // Create F-Parameters
    let f_params = FParameters::new(
        0x0001,  // F-Source Address (Safety Controller)
        0x0100,  // F-Destination Address (Safety Device)
        150,     // 150ms watchdog timeout
        SafetyIntegrityLevel::Sil3,
    )?;
    
    println!("F-Parameters initialized:");
    println!("  Source: 0x{:04X}", f_params.f_source_addr);
    println!("  Destination: 0x{:04X}", f_params.f_dest_addr);
    println!("  Watchdog: {}ms", f_params.f_wd_time);
    println!("  SIL: {:?}", f_params.f_sil);
    
    // Create device manager
    let mut device = ProfisafeDevice::new(f_params)?;
    
    // Prepare safety data
    let safety_data = vec![0x01, 0x02, 0x03, 0x04];
    
    // Send data
    let tx_message = device.send_data(&safety_data)?;
    println!("\nMessage sent:");
    println!("  Toggle: {}", tx_message.toggle_bit);
    println!("  Data: {:02X?}", tx_message.data);
    println!("  CRC: 0x{:08X}", tx_message.crc);
    
    // Encode for transmission
    let encoded = tx_message.encode();
    println!("  Encoded: {:02X?}", encoded);
    
    // Simulate reception
    let rx_message = FSafeMessage::decode(&encoded)?;
    
    // Verify and extract data
    let received_data = device.receive_data(&rx_message)?;
    println!("\nMessage received and verified:");
    println!("  Data: {:02X?}", received_data);
    
    // Check watchdog
    if device.check_watchdog(50) {
        println!("\nWatchdog OK (50ms elapsed)");
    }
    
    Ok(())
}
```

## Summary

F-Parameters and F-Destination Address are fundamental to PROFIsafe safety communication on PROFIBUS networks. The F-Destination Address provides unique device identification independent of physical network addresses, while F-Parameters establish the safety communication relationship with critical settings like watchdog timeouts, CRC configuration, and Safety Integrity Level requirements.

**Key Takeaways:**

- **F-Parameters** define the safety configuration between communicating partners including timeouts, CRC settings, and SIL levels
- **F-Destination Address** uniquely identifies safety devices to prevent device mix-ups
- **Safety mechanisms** include CRC checking, sequence numbering, timeout monitoring, and parameter validation
- **Implementation** requires careful attention to CRC calculations, parameter integrity, and watchdog monitoring
- **Both C/C++ and Rust** can effectively implement PROFIsafe protocols with proper structure and validation
- **Critical for SIL 3** applications requiring the highest level of functional safety in industrial automation

This safety layer enables critical applications like emergency stops, safety door monitoring, light curtains, and other safety-critical I/O to operate reliably over standard PROFIBUS infrastructure.