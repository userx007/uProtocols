# Serial Port Libraries for Modbus Communication

## Overview

Serial port libraries provide the low-level interface for communicating with Modbus RTU devices over RS-232, RS-485, or RS-422 physical layers. Unlike higher-level Modbus libraries that abstract protocol details, serial port libraries give you direct control over the serial communication parameters and data transmission.

## Key Concepts

### Serial Communication Parameters

Serial ports require configuration of several critical parameters:

- **Baud Rate**: Speed of communication (common: 9600, 19200, 38400, 115200 bps)
- **Data Bits**: Typically 8 bits for Modbus
- **Parity**: None, Even, or Odd (Modbus RTU commonly uses Even or None)
- **Stop Bits**: 1 or 2 bits
- **Flow Control**: Hardware (RTS/CTS), Software (XON/XOFF), or None

### Platform-Specific Approaches

Different operating systems provide different APIs:
- **Linux/Unix**: POSIX termios API
- **Windows**: Win32 API (CreateFile, ReadFile, WriteFile, DCB structures)
- **Cross-platform**: Libraries like tokio-serial (Rust) or libserialport

## C/C++ Implementation

### Using termios (Linux/Unix)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <stdint.h>

// Configure serial port for Modbus RTU
int configure_serial_port(int fd, speed_t baud_rate) {
    struct termios options;
    
    // Get current port settings
    if (tcgetattr(fd, &options) < 0) {
        perror("tcgetattr failed");
        return -1;
    }
    
    // Set baud rate
    cfsetispeed(&options, baud_rate);
    cfsetospeed(&options, baud_rate);
    
    // 8N1 mode (8 data bits, no parity, 1 stop bit)
    options.c_cflag &= ~PARENB;  // No parity
    options.c_cflag &= ~CSTOPB;  // 1 stop bit
    options.c_cflag &= ~CSIZE;   // Clear data size bits
    options.c_cflag |= CS8;      // 8 data bits
    
    // Disable hardware flow control
    options.c_cflag &= ~CRTSCTS;
    
    // Enable receiver, ignore modem control lines
    options.c_cflag |= (CLOCAL | CREAD);
    
    // Raw input mode
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    
    // Raw output mode
    options.c_oflag &= ~OPOST;
    
    // Disable software flow control
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    
    // Set read timeout (deciseconds)
    options.c_cc[VMIN] = 0;   // Non-blocking read
    options.c_cc[VTIME] = 10; // 1 second timeout
    
    // Apply settings
    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        perror("tcsetattr failed");
        return -1;
    }
    
    // Flush buffers
    tcflush(fd, TCIOFLUSH);
    
    return 0;
}

// Calculate Modbus RTU CRC16
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

// Send Modbus RTU request
int send_modbus_request(int fd, uint8_t slave_id, uint8_t function_code,
                        uint16_t start_addr, uint16_t quantity) {
    uint8_t request[8];
    
    request[0] = slave_id;
    request[1] = function_code;
    request[2] = (start_addr >> 8) & 0xFF;
    request[3] = start_addr & 0xFF;
    request[4] = (quantity >> 8) & 0xFF;
    request[5] = quantity & 0xFF;
    
    uint16_t crc = calculate_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    ssize_t written = write(fd, request, 8);
    if (written != 8) {
        perror("Write failed");
        return -1;
    }
    
    // Wait for transmission to complete
    tcdrain(fd);
    
    return 0;
}

// Read Modbus RTU response
int read_modbus_response(int fd, uint8_t *buffer, size_t max_size) {
    ssize_t total_read = 0;
    ssize_t bytes_read;
    
    // Read response with timeout
    while (total_read < max_size) {
        bytes_read = read(fd, buffer + total_read, max_size - total_read);
        
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Timeout
            }
            perror("Read failed");
            return -1;
        } else if (bytes_read == 0) {
            break; // No more data
        }
        
        total_read += bytes_read;
        
        // Add small delay between reads
        usleep(10000); // 10ms
    }
    
    return total_read;
}

int main() {
    const char *port_name = "/dev/ttyUSB0";
    int fd;
    
    // Open serial port
    fd = open(port_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("Unable to open serial port");
        return 1;
    }
    
    // Configure for Modbus RTU (9600 baud, 8E1)
    if (configure_serial_port(fd, B9600) < 0) {
        close(fd);
        return 1;
    }
    
    printf("Serial port configured successfully\n");
    
    // Example: Read 10 holding registers from address 0
    if (send_modbus_request(fd, 1, 0x03, 0, 10) < 0) {
        close(fd);
        return 1;
    }
    
    printf("Request sent, waiting for response...\n");
    
    // Read response
    uint8_t response[256];
    int response_len = read_modbus_response(fd, response, sizeof(response));
    
    if (response_len > 0) {
        printf("Received %d bytes:\n", response_len);
        for (int i = 0; i < response_len; i++) {
            printf("%02X ", response[i]);
        }
        printf("\n");
    } else {
        printf("No response received\n");
    }
    
    close(fd);
    return 0;
}
```

### Windows API Version (C++)

```cpp
#include <windows.h>
#include <iostream>
#include <vector>
#include <cstdint>

class SerialPort {
private:
    HANDLE hSerial;
    
public:
    SerialPort() : hSerial(INVALID_HANDLE_VALUE) {}
    
    ~SerialPort() {
        if (hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(hSerial);
        }
    }
    
    bool open(const char* portName, DWORD baudRate) {
        // Open serial port
        hSerial = CreateFileA(portName,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
        
        if (hSerial == INVALID_HANDLE_VALUE) {
            std::cerr << "Error opening serial port" << std::endl;
            return false;
        }
        
        // Configure DCB structure
        DCB dcbSerialParams = {0};
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
        
        if (!GetCommState(hSerial, &dcbSerialParams)) {
            std::cerr << "Error getting device state" << std::endl;
            return false;
        }
        
        dcbSerialParams.BaudRate = baudRate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = EVENPARITY;  // Modbus RTU often uses Even parity
        
        if (!SetCommState(hSerial, &dcbSerialParams)) {
            std::cerr << "Error setting device parameters" << std::endl;
            return false;
        }
        
        // Set timeouts
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 1000;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 1000;
        timeouts.WriteTotalTimeoutMultiplier = 10;
        
        if (!SetCommTimeouts(hSerial, &timeouts)) {
            std::cerr << "Error setting timeouts" << std::endl;
            return false;
        }
        
        // Purge buffers
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
        
        return true;
    }
    
    bool write(const uint8_t* data, size_t length) {
        DWORD bytesWritten;
        
        if (!WriteFile(hSerial, data, length, &bytesWritten, NULL)) {
            std::cerr << "Error writing to serial port" << std::endl;
            return false;
        }
        
        return bytesWritten == length;
    }
    
    int read(uint8_t* buffer, size_t maxSize) {
        DWORD bytesRead;
        
        if (!ReadFile(hSerial, buffer, maxSize, &bytesRead, NULL)) {
            std::cerr << "Error reading from serial port" << std::endl;
            return -1;
        }
        
        return bytesRead;
    }
};

int main() {
    SerialPort port;
    
    if (!port.open("COM3", CBR_9600)) {
        return 1;
    }
    
    std::cout << "Serial port opened successfully" << std::endl;
    
    // Example Modbus request
    uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    
    if (port.write(request, sizeof(request))) {
        std::cout << "Request sent" << std::endl;
        
        uint8_t response[256];
        int bytesRead = port.read(response, sizeof(response));
        
        if (bytesRead > 0) {
            std::cout << "Received " << bytesRead << " bytes" << std::endl;
        }
    }
    
    return 0;
}
```

## Rust Implementation

### Using tokio-serial

```rust
use tokio_serial::{SerialPort, SerialPortBuilderExt};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::time::Duration;

// Calculate Modbus CRC16
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in data {
        crc ^= *byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    crc
}

// Build Modbus RTU request
fn build_modbus_request(
    slave_id: u8,
    function_code: u8,
    start_addr: u16,
    quantity: u16,
) -> Vec<u8> {
    let mut request = Vec::with_capacity(8);
    
    request.push(slave_id);
    request.push(function_code);
    request.push((start_addr >> 8) as u8);
    request.push(start_addr as u8);
    request.push((quantity >> 8) as u8);
    request.push(quantity as u8);
    
    let crc = calculate_crc16(&request);
    request.push(crc as u8);
    request.push((crc >> 8) as u8);
    
    request
}

#[tokio::main]
async fn main() -> tokio_serial::Result<()> {
    // Open and configure serial port
    let mut port = tokio_serial::new("/dev/ttyUSB0", 9600)
        .data_bits(tokio_serial::DataBits::Eight)
        .parity(tokio_serial::Parity::Even)
        .stop_bits(tokio_serial::StopBits::One)
        .timeout(Duration::from_millis(1000))
        .open_native_async()?;
    
    println!("Serial port opened successfully");
    
    // Build Modbus RTU request (Read Holding Registers)
    let request = build_modbus_request(1, 0x03, 0, 10);
    
    // Send request
    port.write_all(&request).await?;
    println!("Sent {} bytes: {:02X?}", request.len(), request);
    
    // Wait for transmission to complete
    tokio::time::sleep(Duration::from_millis(50)).await;
    
    // Read response
    let mut response = vec![0u8; 256];
    match port.read(&mut response).await {
        Ok(bytes_read) => {
            response.truncate(bytes_read);
            println!("Received {} bytes: {:02X?}", bytes_read, response);
            
            // Verify CRC
            if bytes_read >= 4 {
                let data_len = bytes_read - 2;
                let received_crc = u16::from_le_bytes([
                    response[data_len],
                    response[data_len + 1],
                ]);
                let calculated_crc = calculate_crc16(&response[..data_len]);
                
                if received_crc == calculated_crc {
                    println!("CRC valid");
                } else {
                    println!("CRC mismatch!");
                }
            }
        }
        Err(e) => {
            eprintln!("Error reading response: {}", e);
        }
    }
    
    Ok(())
}
```

### Cross-platform Rust with serialport-rs

```rust
use serialport::{SerialPort, DataBits, Parity, StopBits, FlowControl};
use std::time::Duration;
use std::io::{Read, Write};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open serial port with configuration
    let mut port = serialport::new("/dev/ttyUSB0", 9600)
        .data_bits(DataBits::Eight)
        .parity(Parity::Even)
        .stop_bits(StopBits::One)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_millis(1000))
        .open()?;
    
    println!("Port opened: {}", port.name().unwrap_or("Unknown"));
    
    // Modbus RTU request: Read 10 holding registers from address 0
    let request: [u8; 8] = [
        0x01,       // Slave ID
        0x03,       // Function code (Read Holding Registers)
        0x00, 0x00, // Starting address
        0x00, 0x0A, // Quantity
        0xC5, 0xCD, // CRC
    ];
    
    // Send request
    port.write_all(&request)?;
    port.flush()?;
    println!("Request sent");
    
    // Read response
    let mut response = vec![0u8; 256];
    match port.read(&mut response) {
        Ok(bytes_read) => {
            response.truncate(bytes_read);
            println!("Received {} bytes: {:02X?}", bytes_read, response);
        }
        Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {
            println!("Timeout waiting for response");
        }
        Err(e) => return Err(e.into()),
    }
    
    Ok(())
}
```

## Summary

**Serial port libraries** are essential for Modbus RTU communication, providing the foundation for sending and receiving data over physical serial connections. Key considerations include:

- **Platform differences**: termios for Unix/Linux, Windows API for Windows, or cross-platform libraries for portability
- **Configuration precision**: All parameters (baud rate, parity, stop bits) must match between master and slave
- **Timing sensitivity**: Modbus RTU requires careful attention to inter-frame delays and response timeouts
- **Error handling**: Serial communication is prone to noise and requires robust CRC checking and timeout handling
- **Async operations**: Modern implementations (especially in Rust with tokio) benefit from asynchronous I/O for better performance

The examples demonstrate complete workflows from opening ports to sending Modbus requests, showing the low-level details that higher-level Modbus libraries typically abstract away.