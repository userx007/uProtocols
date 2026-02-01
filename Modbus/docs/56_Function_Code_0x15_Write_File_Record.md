# Function Code 0x15: Write File Record

## Detailed Description

Function Code 0x15 (21 decimal) is a Modbus function used to write data to file records in Modbus devices. This function provides a mechanism for writing complex data structures organized in a file/record format, which is more sophisticated than simple register-based operations.

### Key Characteristics

**Purpose**: Write File Record allows clients to write blocks of data to specific file numbers and record numbers within a Modbus server. This is useful for managing structured data that doesn't fit well into the standard holding register model.

**Data Organization**:
- **File Number**: Identifies which file to access (1-65535, where 0 is invalid)
- **Record Number**: Specifies the starting record within the file (0-9999)
- **Record Length**: Number of registers (words) to write per record
- **Reference Type**: Always 0x06 for this function

**Request Structure**:
- Function Code: 1 byte (0x15)
- Request Data Length: 1 byte (total bytes following)
- Sub-requests: Variable length, each containing:
  - Reference Type: 1 byte (0x06)
  - File Number: 2 bytes
  - Record Number: 2 bytes
  - Record Length: 2 bytes (in registers/words)
  - Record Data: N × 2 bytes

**Response Structure**:
- Function Code: 1 byte (0x15)
- Response Data Length: 1 byte
- Echo of request sub-requests (same format as request)

**Constraints**:
- Maximum of 247 bytes in the request/response data field
- Multiple sub-requests can be batched in a single transaction
- Each record is composed of 2-byte registers

### Use Cases

1. **Configuration Management**: Storing device configuration in structured files
2. **Data Logging**: Writing time-series data to sequential records
3. **Recipe Storage**: Storing production recipes or parameter sets
4. **Firmware Updates**: Writing firmware blocks to file records
5. **Complex Data Structures**: Managing hierarchical or structured data

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

// Modbus Write File Record structures
#define MODBUS_FC_WRITE_FILE_RECORD 0x15
#define MAX_RECORD_DATA_LENGTH 245

typedef struct {
    uint8_t reference_type;    // Always 0x06
    uint16_t file_number;      // 1-65535
    uint16_t record_number;    // 0-9999
    uint16_t record_length;    // Number of registers
    uint16_t *record_data;     // Pointer to register data
} modbus_file_subrequest_t;

typedef struct {
    uint8_t function_code;
    uint8_t byte_count;
    uint8_t data[MAX_RECORD_DATA_LENGTH];
} modbus_write_file_record_request_t;

/**
 * Build a Write File Record request
 * 
 * @param request Pointer to request structure to populate
 * @param subrequests Array of subrequest structures
 * @param num_subrequests Number of subrequests
 * @return Total request length in bytes, or -1 on error
 */
int modbus_build_write_file_record_request(
    modbus_write_file_record_request_t *request,
    modbus_file_subrequest_t *subrequests,
    int num_subrequests)
{
    if (!request || !subrequests || num_subrequests < 1) {
        return -1;
    }

    request->function_code = MODBUS_FC_WRITE_FILE_RECORD;
    
    uint8_t *ptr = request->data;
    int total_bytes = 0;

    for (int i = 0; i < num_subrequests; i++) {
        modbus_file_subrequest_t *sub = &subrequests[i];
        
        // Validate subrequest
        if (sub->reference_type != 0x06 || 
            sub->file_number == 0 || 
            sub->record_number > 9999 ||
            sub->record_length == 0) {
            return -1;
        }

        // Calculate subrequest size
        int subrequest_size = 7 + (sub->record_length * 2);
        
        // Check if we exceed maximum size
        if (total_bytes + subrequest_size > MAX_RECORD_DATA_LENGTH) {
            return -1;
        }

        // Reference Type
        *ptr++ = sub->reference_type;
        
        // File Number (big-endian)
        *ptr++ = (sub->file_number >> 8) & 0xFF;
        *ptr++ = sub->file_number & 0xFF;
        
        // Record Number (big-endian)
        *ptr++ = (sub->record_number >> 8) & 0xFF;
        *ptr++ = sub->record_number & 0xFF;
        
        // Record Length (big-endian)
        *ptr++ = (sub->record_length >> 8) & 0xFF;
        *ptr++ = sub->record_length & 0xFF;
        
        // Record Data (big-endian)
        for (int j = 0; j < sub->record_length; j++) {
            *ptr++ = (sub->record_data[j] >> 8) & 0xFF;
            *ptr++ = sub->record_data[j] & 0xFF;
        }
        
        total_bytes += subrequest_size;
    }

    request->byte_count = total_bytes;
    return total_bytes + 2; // +2 for function code and byte count
}

/**
 * Parse Write File Record response
 * 
 * @param response Pointer to response buffer
 * @param response_len Length of response
 * @return 0 on success, -1 on error
 */
int modbus_parse_write_file_record_response(
    const uint8_t *response,
    int response_len)
{
    if (!response || response_len < 4) {
        return -1;
    }

    // Check function code
    if (response[0] != MODBUS_FC_WRITE_FILE_RECORD) {
        // Check for exception
        if (response[0] == (MODBUS_FC_WRITE_FILE_RECORD | 0x80)) {
            printf("Modbus Exception: 0x%02X\n", response[1]);
        }
        return -1;
    }

    uint8_t byte_count = response[1];
    
    if (response_len < byte_count + 2) {
        printf("Invalid response length\n");
        return -1;
    }

    printf("Write File Record successful, %d bytes written\n", byte_count);
    return 0;
}

/**
 * Example: Write configuration data to file records
 */
void example_write_file_record() {
    modbus_write_file_record_request_t request;
    
    // Example: Write two records to file 4
    uint16_t config_data1[] = {0x1234, 0x5678, 0xABCD};
    uint16_t config_data2[] = {0x9999, 0x8888};
    
    modbus_file_subrequest_t subrequests[2] = {
        {
            .reference_type = 0x06,
            .file_number = 4,
            .record_number = 1,
            .record_length = 3,
            .record_data = config_data1
        },
        {
            .reference_type = 0x06,
            .file_number = 4,
            .record_number = 7,
            .record_length = 2,
            .record_data = config_data2
        }
    };
    
    int request_len = modbus_build_write_file_record_request(
        &request, subrequests, 2);
    
    if (request_len > 0) {
        printf("Write File Record Request built: %d bytes\n", request_len);
        printf("Function Code: 0x%02X\n", request.function_code);
        printf("Byte Count: %d\n", request.byte_count);
        
        // Here you would send the request via TCP/RTU
        // and receive the response
        
        // Example response (echo of request)
        uint8_t response[256];
        response[0] = MODBUS_FC_WRITE_FILE_RECORD;
        response[1] = request.byte_count;
        memcpy(&response[2], request.data, request.byte_count);
        
        modbus_parse_write_file_record_response(
            response, request.byte_count + 2);
    }
}

int main() {
    printf("=== Modbus Write File Record Example ===\n\n");
    example_write_file_record();
    return 0;
}
```

---

## Rust Implementation

```rust
use std::io::{self, Write};

/// Modbus Function Code for Write File Record
const MODBUS_FC_WRITE_FILE_RECORD: u8 = 0x15;
const MAX_RECORD_DATA_LENGTH: usize = 245;

/// Represents a single file record subrequest
#[derive(Debug, Clone)]
pub struct FileSubrequest {
    pub reference_type: u8,      // Always 0x06
    pub file_number: u16,        // 1-65535
    pub record_number: u16,      // 0-9999
    pub record_data: Vec<u16>,   // Register data
}

impl FileSubrequest {
    /// Create a new file subrequest
    pub fn new(file_number: u16, record_number: u16, data: Vec<u16>) -> Result<Self, String> {
        if file_number == 0 {
            return Err("File number cannot be 0".to_string());
        }
        if record_number > 9999 {
            return Err("Record number must be <= 9999".to_string());
        }
        if data.is_empty() {
            return Err("Record data cannot be empty".to_string());
        }
        
        Ok(FileSubrequest {
            reference_type: 0x06,
            file_number,
            record_number,
            record_data: data,
        })
    }
    
    /// Calculate the size of this subrequest in bytes
    pub fn size(&self) -> usize {
        7 + (self.record_data.len() * 2)
    }
    
    /// Encode this subrequest to bytes
    pub fn encode(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        
        // Reference Type
        bytes.push(self.reference_type);
        
        // File Number (big-endian)
        bytes.extend_from_slice(&self.file_number.to_be_bytes());
        
        // Record Number (big-endian)
        bytes.extend_from_slice(&self.record_number.to_be_bytes());
        
        // Record Length (big-endian)
        let record_length = self.record_data.len() as u16;
        bytes.extend_from_slice(&record_length.to_be_bytes());
        
        // Record Data (big-endian)
        for &value in &self.record_data {
            bytes.extend_from_slice(&value.to_be_bytes());
        }
        
        bytes
    }
}

/// Write File Record Request
#[derive(Debug)]
pub struct WriteFileRecordRequest {
    pub subrequests: Vec<FileSubrequest>,
}

impl WriteFileRecordRequest {
    /// Create a new Write File Record request
    pub fn new(subrequests: Vec<FileSubrequest>) -> Result<Self, String> {
        if subrequests.is_empty() {
            return Err("At least one subrequest is required".to_string());
        }
        
        // Calculate total size
        let total_size: usize = subrequests.iter().map(|s| s.size()).sum();
        
        if total_size > MAX_RECORD_DATA_LENGTH {
            return Err(format!(
                "Total request size ({}) exceeds maximum ({})",
                total_size, MAX_RECORD_DATA_LENGTH
            ));
        }
        
        Ok(WriteFileRecordRequest { subrequests })
    }
    
    /// Encode the request to bytes
    pub fn encode(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        
        // Function Code
        bytes.push(MODBUS_FC_WRITE_FILE_RECORD);
        
        // Encode all subrequests
        let mut data = Vec::new();
        for subrequest in &self.subrequests {
            data.extend(subrequest.encode());
        }
        
        // Byte Count
        bytes.push(data.len() as u8);
        
        // Data
        bytes.extend(data);
        
        bytes
    }
}

/// Write File Record Response
#[derive(Debug)]
pub struct WriteFileRecordResponse {
    pub byte_count: u8,
    pub data: Vec<u8>,
}

impl WriteFileRecordResponse {
    /// Parse a response from bytes
    pub fn parse(data: &[u8]) -> Result<Self, String> {
        if data.len() < 2 {
            return Err("Response too short".to_string());
        }
        
        let function_code = data[0];
        
        // Check for exception
        if function_code & 0x80 != 0 {
            let exception_code = data.get(1).copied().unwrap_or(0);
            return Err(format!("Modbus Exception: 0x{:02X}", exception_code));
        }
        
        if function_code != MODBUS_FC_WRITE_FILE_RECORD {
            return Err(format!("Invalid function code: 0x{:02X}", function_code));
        }
        
        let byte_count = data[1];
        
        if data.len() < (byte_count as usize + 2) {
            return Err("Invalid response length".to_string());
        }
        
        Ok(WriteFileRecordResponse {
            byte_count,
            data: data[2..2 + byte_count as usize].to_vec(),
        })
    }
}

/// Example: Write File Record client
pub struct ModbusClient;

impl ModbusClient {
    /// Write file records
    pub fn write_file_record(
        &self,
        subrequests: Vec<FileSubrequest>,
    ) -> Result<WriteFileRecordResponse, String> {
        // Build request
        let request = WriteFileRecordRequest::new(subrequests)?;
        let request_bytes = request.encode();
        
        println!("Sending Write File Record request ({} bytes)", request_bytes.len());
        println!("Request: {:02X?}", request_bytes);
        
        // In a real implementation, send via TCP/RTU here
        // For this example, we'll simulate an echo response
        
        let response_bytes = request_bytes; // Echo for simulation
        
        // Parse response
        WriteFileRecordResponse::parse(&response_bytes)
    }
}

/// Example usage
fn main() -> Result<(), String> {
    println!("=== Modbus Write File Record Example ===\n");
    
    let client = ModbusClient;
    
    // Example 1: Write configuration data to file 4, record 1
    let config_data = vec![0x1234, 0x5678, 0xABCD, 0xEF01];
    let subrequest1 = FileSubrequest::new(4, 1, config_data)?;
    
    // Example 2: Write calibration data to file 5, record 10
    let calibration_data = vec![0x0100, 0x0200];
    let subrequest2 = FileSubrequest::new(5, 10, calibration_data)?;
    
    // Send write request with multiple subrequests
    let response = client.write_file_record(vec![subrequest1, subrequest2])?;
    
    println!("\nResponse received:");
    println!("Byte Count: {}", response.byte_count);
    println!("Data: {:02X?}", response.data);
    println!("\n✓ Write File Record successful!");
    
    // Example 3: Write a single large record
    println!("\n--- Writing large data block ---");
    let large_data: Vec<u16> = (0..50).map(|i| i * 100).collect();
    let large_subrequest = FileSubrequest::new(1, 0, large_data)?;
    
    let response2 = client.write_file_record(vec![large_subrequest])?;
    println!("Large write successful: {} bytes", response2.byte_count);
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_subrequest_creation() {
        let data = vec![0x1234, 0x5678];
        let subreq = FileSubrequest::new(4, 1, data).unwrap();
        
        assert_eq!(subreq.reference_type, 0x06);
        assert_eq!(subreq.file_number, 4);
        assert_eq!(subreq.record_number, 1);
        assert_eq!(subreq.record_data.len(), 2);
    }
    
    #[test]
    fn test_subrequest_encoding() {
        let data = vec![0x0001, 0x0002];
        let subreq = FileSubrequest::new(1, 0, data).unwrap();
        let encoded = subreq.encode();
        
        // Reference Type (1) + File# (2) + Record# (2) + Length (2) + Data (4) = 11 bytes
        assert_eq!(encoded.len(), 11);
        assert_eq!(encoded[0], 0x06); // Reference type
        assert_eq!(encoded[1..3], [0x00, 0x01]); // File number
    }
    
    #[test]
    fn test_invalid_file_number() {
        let data = vec![0x1234];
        let result = FileSubrequest::new(0, 1, data);
        assert!(result.is_err());
    }
    
    #[test]
    fn test_request_encoding() {
        let subreq = FileSubrequest::new(4, 7, vec![0x0000, 0x0001]).unwrap();
        let request = WriteFileRecordRequest::new(vec![subreq]).unwrap();
        let encoded = request.encode();
        
        assert_eq!(encoded[0], MODBUS_FC_WRITE_FILE_RECORD);
        assert!(encoded[1] > 0); // Byte count should be positive
    }
}
```

---

## Summary

**Function Code 0x15 (Write File Record)** is a sophisticated Modbus function that enables writing structured data organized in files and records, going beyond simple register-based operations. It's particularly valuable for managing complex configuration data, logging, and hierarchical information structures.

**Key Points**:

- **Structured Storage**: Organizes data into files and records rather than flat register spaces
- **Batch Operations**: Supports multiple sub-requests in a single transaction for efficiency
- **Flexible Size**: Each sub-request can write variable-length records (multiple registers)
- **Reference Type**: Always uses 0x06 to identify file record operations
- **Echo Response**: The server echoes back the request data to confirm the write operation

**Implementation Considerations**:

1. **Validation**: Always validate file numbers (≠0), record numbers (≤9999), and total message size (≤247 bytes)
2. **Byte Order**: All multi-byte values use big-endian (network) byte order
3. **Error Handling**: Check for Modbus exceptions and validate response structure
4. **Memory Management**: In C/C++, carefully manage dynamic memory for variable-length records
5. **Type Safety**: Rust's type system provides excellent compile-time guarantees for protocol correctness

The implementations above demonstrate complete encoding/decoding for this function code, with proper error handling and examples showing both single and multiple sub-request scenarios.