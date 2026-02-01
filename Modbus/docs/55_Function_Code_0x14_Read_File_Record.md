# Function Code 0x14: Read File Record

## Detailed Description

Modbus Function Code 0x14 (decimal 20) is used to read the contents of file records from a Modbus slave device. This function provides access to organized, file-based memory structures within the slave, which is particularly useful for devices that store configuration data, historical logs, or other structured information in a file-record format.

### Key Characteristics

**Purpose**: Read File Record allows a master to retrieve data from specific file records in the slave device's memory. Unlike standard register reads, this function accesses data organized in a file system-like structure with file numbers and record numbers.

**Data Organization**: 
- **File Number**: A 16-bit value (0x0001 to 0xFFFF) identifying the file
- **Record Number**: A 16-bit value identifying the specific record within the file
- **Record Length**: Number of registers (16-bit words) to read from the record

**Use Cases**:
- Reading configuration profiles stored in files
- Accessing historical data logs
- Retrieving recipe data or parameter sets
- Reading structured databases within industrial devices

### Request Structure

The request consists of:
- **Byte Count**: Total number of bytes in the sub-request(s)
- **Sub-requests**: One or more sub-request structures, each containing:
  - Reference Type (1 byte): Always 0x06 for standard file record access
  - File Number (2 bytes): The file to access
  - Record Number (2 bytes): Starting record number
  - Record Length (2 bytes): Number of registers to read

### Response Structure

The response contains:
- **Response Data Length**: Total byte count of the response data
- **Sub-responses**: Corresponding to each sub-request:
  - File Response Length (1 byte)
  - Reference Type (1 byte): Echo of request (0x06)
  - Record Data (N bytes): The actual register data (2 bytes per register)

### Error Handling

Common exception codes:
- **0x01 (Illegal Function)**: Function not supported
- **0x02 (Illegal Data Address)**: Invalid file number or record number
- **0x03 (Illegal Data Value)**: Invalid record length or byte count
- **0x04 (Slave Device Failure)**: File system error
- **0x08 (Memory Parity Error)**: File record corruption

## C/C++ Programming Example

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Modbus function code
#define MODBUS_FC_READ_FILE_RECORD 0x14
#define MODBUS_REFERENCE_TYPE 0x06

// Maximum sizes
#define MAX_FILE_RECORDS 35  // Maximum per Modbus spec
#define MAX_PDU_SIZE 253

// Exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03

// Sub-request structure
typedef struct {
    uint8_t reference_type;
    uint16_t file_number;
    uint16_t record_number;
    uint16_t record_length;  // in registers (words)
} modbus_file_subrequest_t;

// Build Read File Record request
int modbus_build_read_file_record_request(
    uint8_t *request,
    size_t max_len,
    modbus_file_subrequest_t *subrequests,
    uint8_t num_subrequests)
{
    if (!request || !subrequests || num_subrequests == 0) {
        return -1;
    }

    // Calculate byte count (7 bytes per sub-request)
    uint8_t byte_count = num_subrequests * 7;
    
    // Check total PDU size
    if (2 + byte_count > max_len) {
        return -1;
    }

    uint8_t *ptr = request;
    
    // Function code
    *ptr++ = MODBUS_FC_READ_FILE_RECORD;
    
    // Byte count
    *ptr++ = byte_count;
    
    // Add each sub-request
    for (int i = 0; i < num_subrequests; i++) {
        *ptr++ = subrequests[i].reference_type;
        
        // File number (big-endian)
        *ptr++ = (subrequests[i].file_number >> 8) & 0xFF;
        *ptr++ = subrequests[i].file_number & 0xFF;
        
        // Record number (big-endian)
        *ptr++ = (subrequests[i].record_number >> 8) & 0xFF;
        *ptr++ = subrequests[i].record_number & 0xFF;
        
        // Record length (big-endian)
        *ptr++ = (subrequests[i].record_length >> 8) & 0xFF;
        *ptr++ = subrequests[i].record_length & 0xFF;
    }
    
    return ptr - request;
}

// Parse Read File Record response
int modbus_parse_read_file_record_response(
    uint8_t *response,
    size_t response_len,
    uint16_t *output_data,
    size_t max_output_registers)
{
    if (!response || response_len < 2) {
        return -1;
    }
    
    // Check function code
    if (response[0] != MODBUS_FC_READ_FILE_RECORD) {
        // Check for exception
        if (response[0] == (MODBUS_FC_READ_FILE_RECORD | 0x80)) {
            printf("Modbus Exception: 0x%02X\n", response[1]);
            return -response[1];
        }
        return -1;
    }
    
    uint8_t resp_data_length = response[1];
    
    if (2 + resp_data_length > response_len) {
        return -1;
    }
    
    uint8_t *ptr = response + 2;
    uint16_t total_registers = 0;
    
    // Parse sub-responses
    while (ptr < response + 2 + resp_data_length) {
        uint8_t file_resp_len = *ptr++;
        uint8_t ref_type = *ptr++;
        
        if (ref_type != MODBUS_REFERENCE_TYPE) {
            return -1;
        }
        
        // Data length is file_resp_len - 1 (minus ref_type byte)
        uint8_t data_bytes = file_resp_len - 1;
        uint16_t num_registers = data_bytes / 2;
        
        // Check output buffer size
        if (total_registers + num_registers > max_output_registers) {
            return -1;
        }
        
        // Extract register data (big-endian)
        for (int i = 0; i < num_registers; i++) {
            output_data[total_registers++] = (*ptr << 8) | *(ptr + 1);
            ptr += 2;
        }
    }
    
    return total_registers;
}

// Slave-side: Process Read File Record request
int modbus_slave_process_read_file_record(
    uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t max_response_len,
    uint16_t *file_memory,  // Simulated file storage
    size_t file_memory_size)
{
    if (request[0] != MODBUS_FC_READ_FILE_RECORD || request_len < 2) {
        return -1;
    }
    
    uint8_t byte_count = request[1];
    
    if (2 + byte_count > request_len) {
        // Exception: Illegal Data Value
        response[0] = MODBUS_FC_READ_FILE_RECORD | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        return 2;
    }
    
    // Build response
    uint8_t *req_ptr = request + 2;
    uint8_t *resp_ptr = response + 2;
    uint8_t total_resp_bytes = 0;
    
    response[0] = MODBUS_FC_READ_FILE_RECORD;
    
    while (req_ptr < request + 2 + byte_count) {
        uint8_t ref_type = *req_ptr++;
        uint16_t file_num = (*req_ptr << 8) | *(req_ptr + 1);
        req_ptr += 2;
        uint16_t record_num = (*req_ptr << 8) | *(req_ptr + 1);
        req_ptr += 2;
        uint16_t record_len = (*req_ptr << 8) | *(req_ptr + 1);
        req_ptr += 2;
        
        // Validate reference type
        if (ref_type != MODBUS_REFERENCE_TYPE) {
            response[0] = MODBUS_FC_READ_FILE_RECORD | 0x80;
            response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
            return 2;
        }
        
        // Simple validation (in real implementation, check file system)
        if (file_num == 0 || record_len == 0 || 
            record_num + record_len > file_memory_size) {
            response[0] = MODBUS_FC_READ_FILE_RECORD | 0x80;
            response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            return 2;
        }
        
        // File response length: 1 (ref_type) + 2*record_len (data)
        uint8_t file_resp_len = 1 + (record_len * 2);
        *resp_ptr++ = file_resp_len;
        *resp_ptr++ = ref_type;
        
        // Copy register data
        for (int i = 0; i < record_len; i++) {
            uint16_t value = file_memory[record_num + i];
            *resp_ptr++ = (value >> 8) & 0xFF;
            *resp_ptr++ = value & 0xFF;
        }
        
        total_resp_bytes += file_resp_len + 1;  // +1 for length byte
    }
    
    response[1] = total_resp_bytes;
    
    return 2 + total_resp_bytes;
}

// Example usage
int main() {
    uint8_t request[MAX_PDU_SIZE];
    uint8_t response[MAX_PDU_SIZE];
    
    // Example: Read 10 registers from file 1, record 0
    modbus_file_subrequest_t subreq = {
        .reference_type = MODBUS_REFERENCE_TYPE,
        .file_number = 1,
        .record_number = 0,
        .record_length = 10
    };
    
    int req_len = modbus_build_read_file_record_request(
        request, sizeof(request), &subreq, 1);
    
    printf("Request (%d bytes): ", req_len);
    for (int i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    // Simulate slave processing
    uint16_t file_data[100];
    for (int i = 0; i < 100; i++) {
        file_data[i] = 1000 + i;  // Test data
    }
    
    int resp_len = modbus_slave_process_read_file_record(
        request, req_len, response, sizeof(response),
        file_data, 100);
    
    printf("Response (%d bytes): ", resp_len);
    for (int i = 0; i < resp_len; i++) {
        printf("%02X ", response[i]);
    }
    printf("\n");
    
    // Parse response
    uint16_t output[50];
    int num_regs = modbus_parse_read_file_record_response(
        response, resp_len, output, 50);
    
    printf("\nRead %d registers:\n", num_regs);
    for (int i = 0; i < num_regs; i++) {
        printf("Register %d: %u\n", i, output[i]);
    }
    
    return 0;
}
```

## Rust Programming Example

```rust
use std::io::{self, Error, ErrorKind};

// Modbus constants
const MODBUS_FC_READ_FILE_RECORD: u8 = 0x14;
const MODBUS_REFERENCE_TYPE: u8 = 0x06;
const MAX_PDU_SIZE: usize = 253;

// Exception codes
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum ModbusException {
    IllegalFunction = 0x01,
    IllegalDataAddress = 0x02,
    IllegalDataValue = 0x03,
    SlaveDeviceFailure = 0x04,
    MemoryParityError = 0x08,
}

// Sub-request structure
#[derive(Debug, Clone)]
pub struct FileSubRequest {
    pub reference_type: u8,
    pub file_number: u16,
    pub record_number: u16,
    pub record_length: u16, // in registers
}

impl FileSubRequest {
    pub fn new(file_number: u16, record_number: u16, record_length: u16) -> Self {
        Self {
            reference_type: MODBUS_REFERENCE_TYPE,
            file_number,
            record_number,
            record_length,
        }
    }
}

// Build Read File Record request
pub fn build_read_file_record_request(
    subrequests: &[FileSubRequest],
) -> io::Result<Vec<u8>> {
    if subrequests.is_empty() {
        return Err(Error::new(ErrorKind::InvalidInput, "No sub-requests provided"));
    }

    let byte_count = subrequests.len() * 7;
    
    if 2 + byte_count > MAX_PDU_SIZE {
        return Err(Error::new(ErrorKind::InvalidInput, "Request too large"));
    }

    let mut request = Vec::with_capacity(2 + byte_count);
    
    // Function code
    request.push(MODBUS_FC_READ_FILE_RECORD);
    
    // Byte count
    request.push(byte_count as u8);
    
    // Add each sub-request
    for subreq in subrequests {
        request.push(subreq.reference_type);
        request.extend_from_slice(&subreq.file_number.to_be_bytes());
        request.extend_from_slice(&subreq.record_number.to_be_bytes());
        request.extend_from_slice(&subreq.record_length.to_be_bytes());
    }
    
    Ok(request)
}

// Parse Read File Record response
pub fn parse_read_file_record_response(response: &[u8]) -> io::Result<Vec<u16>> {
    if response.len() < 2 {
        return Err(Error::new(ErrorKind::InvalidData, "Response too short"));
    }
    
    // Check for exception
    if response[0] == (MODBUS_FC_READ_FILE_RECORD | 0x80) {
        return Err(Error::new(
            ErrorKind::Other,
            format!("Modbus exception: 0x{:02X}", response[1]),
        ));
    }
    
    if response[0] != MODBUS_FC_READ_FILE_RECORD {
        return Err(Error::new(ErrorKind::InvalidData, "Invalid function code"));
    }
    
    let resp_data_length = response[1] as usize;
    
    if 2 + resp_data_length > response.len() {
        return Err(Error::new(ErrorKind::InvalidData, "Invalid response length"));
    }
    
    let mut output = Vec::new();
    let mut ptr = 2;
    
    while ptr < 2 + resp_data_length {
        let file_resp_len = response[ptr] as usize;
        ptr += 1;
        
        let ref_type = response[ptr];
        ptr += 1;
        
        if ref_type != MODBUS_REFERENCE_TYPE {
            return Err(Error::new(ErrorKind::InvalidData, "Invalid reference type"));
        }
        
        // Data length is file_resp_len - 1
        let data_bytes = file_resp_len - 1;
        let num_registers = data_bytes / 2;
        
        for _ in 0..num_registers {
            if ptr + 1 >= response.len() {
                return Err(Error::new(ErrorKind::InvalidData, "Unexpected end of data"));
            }
            let value = u16::from_be_bytes([response[ptr], response[ptr + 1]]);
            output.push(value);
            ptr += 2;
        }
    }
    
    Ok(output)
}

// Slave-side processing
pub struct ModbusFileSlave {
    file_memory: Vec<u16>,
}

impl ModbusFileSlave {
    pub fn new(size: usize) -> Self {
        Self {
            file_memory: vec![0; size],
        }
    }
    
    pub fn set_register(&mut self, index: usize, value: u16) -> io::Result<()> {
        if index >= self.file_memory.len() {
            return Err(Error::new(ErrorKind::InvalidInput, "Index out of bounds"));
        }
        self.file_memory[index] = value;
        Ok(())
    }
    
    pub fn process_read_file_record(&self, request: &[u8]) -> io::Result<Vec<u8>> {
        if request.len() < 2 {
            return self.create_exception_response(ModbusException::IllegalDataValue);
        }
        
        if request[0] != MODBUS_FC_READ_FILE_RECORD {
            return self.create_exception_response(ModbusException::IllegalFunction);
        }
        
        let byte_count = request[1] as usize;
        
        if 2 + byte_count > request.len() {
            return self.create_exception_response(ModbusException::IllegalDataValue);
        }
        
        let mut response = vec![MODBUS_FC_READ_FILE_RECORD, 0]; // Placeholder for length
        let mut req_ptr = 2;
        
        while req_ptr < 2 + byte_count {
            if req_ptr + 7 > request.len() {
                return self.create_exception_response(ModbusException::IllegalDataValue);
            }
            
            let ref_type = request[req_ptr];
            let file_num = u16::from_be_bytes([request[req_ptr + 1], request[req_ptr + 2]]);
            let record_num = u16::from_be_bytes([request[req_ptr + 3], request[req_ptr + 4]]);
            let record_len = u16::from_be_bytes([request[req_ptr + 5], request[req_ptr + 6]]);
            req_ptr += 7;
            
            // Validate
            if ref_type != MODBUS_REFERENCE_TYPE {
                return self.create_exception_response(ModbusException::IllegalDataValue);
            }
            
            if file_num == 0 || record_len == 0 {
                return self.create_exception_response(ModbusException::IllegalDataAddress);
            }
            
            let end_index = record_num as usize + record_len as usize;
            if end_index > self.file_memory.len() {
                return self.create_exception_response(ModbusException::IllegalDataAddress);
            }
            
            // File response length
            let file_resp_len = 1 + (record_len * 2);
            response.push(file_resp_len as u8);
            response.push(ref_type);
            
            // Copy register data
            for i in 0..record_len as usize {
                let value = self.file_memory[record_num as usize + i];
                response.extend_from_slice(&value.to_be_bytes());
            }
        }
        
        // Set total response data length
        response[1] = (response.len() - 2) as u8;
        
        Ok(response)
    }
    
    fn create_exception_response(&self, exception: ModbusException) -> io::Result<Vec<u8>> {
        Ok(vec![MODBUS_FC_READ_FILE_RECORD | 0x80, exception as u8])
    }
}

// Example usage
fn main() -> io::Result<()> {
    println!("Modbus Read File Record Example\n");
    
    // Create a request
    let subrequest = FileSubRequest::new(1, 0, 10);
    let request = build_read_file_record_request(&[subrequest])?;
    
    println!("Request ({} bytes):", request.len());
    for byte in &request {
        print!("{:02X} ", byte);
    }
    println!("\n");
    
    // Simulate slave processing
    let mut slave = ModbusFileSlave::new(100);
    
    // Initialize with test data
    for i in 0..100 {
        slave.set_register(i, 1000 + i as u16)?;
    }
    
    let response = slave.process_read_file_record(&request)?;
    
    println!("Response ({} bytes):", response.len());
    for byte in &response {
        print!("{:02X} ", byte);
    }
    println!("\n");
    
    // Parse response
    let registers = parse_read_file_record_response(&response)?;
    
    println!("Read {} registers:", registers.len());
    for (i, &value) in registers.iter().enumerate() {
        println!("Register {}: {}", i, value);
    }
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_build_request() {
        let subreq = FileSubRequest::new(1, 5, 3);
        let request = build_read_file_record_request(&[subreq]).unwrap();
        
        assert_eq!(request[0], MODBUS_FC_READ_FILE_RECORD);
        assert_eq!(request[1], 7); // Byte count
        assert_eq!(request[2], MODBUS_REFERENCE_TYPE);
    }
    
    #[test]
    fn test_slave_processing() {
        let mut slave = ModbusFileSlave::new(10);
        slave.set_register(0, 100).unwrap();
        slave.set_register(1, 200).unwrap();
        
        let subreq = FileSubRequest::new(1, 0, 2);
        let request = build_read_file_record_request(&[subreq]).unwrap();
        
        let response = slave.process_read_file_record(&request).unwrap();
        let registers = parse_read_file_record_response(&response).unwrap();
        
        assert_eq!(registers.len(), 2);
        assert_eq!(registers[0], 100);
        assert_eq!(registers[1], 200);
    }
}
```

## Summary

**Modbus Function Code 0x14 (Read File Record)** provides structured access to file-based memory in slave devices, enabling masters to read data organized in files and records rather than linear register spaces.

**Key Features**:
- Supports multiple sub-requests in a single transaction for efficient batch reads
- Uses file numbers and record numbers for hierarchical data organization
- Each record consists of 16-bit registers (words)
- Reference type 0x06 indicates standard file record access
- Maximum request size limited by Modbus PDU constraints (typically 253 bytes)

**Practical Applications**:
- Configuration management with multiple parameter sets
- Historical data retrieval from device logs
- Recipe or profile storage in industrial equipment
- Structured database access in intelligent devices

**Implementation Considerations**:
- Proper validation of file numbers, record numbers, and lengths is critical
- Big-endian byte order must be maintained for all multi-byte values
- Error handling should cover file system errors and boundary conditions
- Response parsing must handle variable-length sub-responses correctly

This function is particularly valuable in applications requiring organized data storage beyond simple register arrays, providing a more sophisticated memory model for complex industrial devices.