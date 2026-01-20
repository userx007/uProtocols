# Serial Port Configuration in Modbus

## Overview

Serial port configuration is fundamental to Modbus RTU and ASCII communication. The serial parameters must match exactly between master and slave devices for successful data exchange. These parameters define how data is encoded, transmitted, and verified over RS-232, RS-485, or RS-422 physical layers.

## Key Configuration Parameters

### 1. **Baud Rate**
The speed of data transmission measured in bits per second (bps). Common rates:
- 9600 bps (most common default)
- 19200 bps
- 38400 bps
- 57600 bps
- 115200 bps

### 2. **Parity**
Error detection mechanism:
- **None (N)**: No parity checking
- **Even (E)**: Even number of 1 bits
- **Odd (O)**: Odd number of 1 bits

### 3. **Data Bits**
Number of bits in each character:
- **7 bits**: Used with parity
- **8 bits**: Most common, typically with no parity

### 4. **Stop Bits**
Marks the end of a byte:
- **1 stop bit**: Standard configuration
- **2 stop bits**: Used for slower, more reliable communication

### 5. **Common Configurations**
- RTU: 8-N-1 (8 data bits, No parity, 1 stop bit) or 8-E-1
- ASCII: 7-E-1 (7 data bits, Even parity, 1 stop bit)

---

## C/C++ Implementation

### Linux/POSIX Serial Configuration

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

typedef enum {
    PARITY_NONE,
    PARITY_EVEN,
    PARITY_ODD
} parity_t;

typedef struct {
    int fd;
    char port[256];
    int baud_rate;
    int data_bits;
    parity_t parity;
    int stop_bits;
} modbus_serial_t;

// Convert baud rate integer to termios constant
speed_t get_baud_constant(int baud) {
    switch(baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return B9600;
    }
}

// Configure serial port for Modbus RTU
int modbus_serial_configure(modbus_serial_t *ctx) {
    struct termios tty;
    
    // Open serial port
    ctx->fd = open(ctx->port, O_RDWR | O_NOCTTY | O_SYNC);
    if (ctx->fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", ctx->port, strerror(errno));
        return -1;
    }
    
    // Get current configuration
    if (tcgetattr(ctx->fd, &tty) != 0) {
        fprintf(stderr, "Error getting attributes: %s\n", strerror(errno));
        close(ctx->fd);
        return -1;
    }
    
    // Set baud rate
    speed_t baud = get_baud_constant(ctx->baud_rate);
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    
    // Configure control modes
    tty.c_cflag &= ~CSIZE; // Clear data bit mask
    
    // Set data bits
    switch(ctx->data_bits) {
        case 7:
            tty.c_cflag |= CS7;
            break;
        case 8:
            tty.c_cflag |= CS8;
            break;
        default:
            tty.c_cflag |= CS8;
    }
    
    // Set parity
    switch(ctx->parity) {
        case PARITY_NONE:
            tty.c_cflag &= ~PARENB;
            break;
        case PARITY_EVEN:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case PARITY_ODD:
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
            break;
    }
    
    // Set stop bits
    if (ctx->stop_bits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }
    
    // Enable receiver, ignore modem control lines
    tty.c_cflag |= (CLOCAL | CREAD);
    
    // Disable hardware flow control
    tty.c_cflag &= ~CRTSCTS;
    
    // Configure input modes (non-canonical, no echo)
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    
    // Configure output modes (raw output)
    tty.c_oflag &= ~OPOST;
    
    // Configure local modes
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    
    // Set timeout (deciseconds)
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10; // 1 second timeout
    
    // Apply configuration
    if (tcsetattr(ctx->fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Error setting attributes: %s\n", strerror(errno));
        close(ctx->fd);
        return -1;
    }
    
    // Flush buffers
    tcflush(ctx->fd, TCIOFLUSH);
    
    return 0;
}

// Example usage
int main() {
    modbus_serial_t modbus = {
        .port = "/dev/ttyUSB0",
        .baud_rate = 9600,
        .data_bits = 8,
        .parity = PARITY_NONE,
        .stop_bits = 1
    };
    
    if (modbus_serial_configure(&modbus) == 0) {
        printf("Serial port configured successfully: 8-N-1 @ 9600 bps\n");
        
        // Your Modbus communication code here
        
        close(modbus.fd);
    }
    
    return 0;
}
```

### Windows Serial Configuration

```c
#include <windows.h>
#include <stdio.h>

typedef struct {
    HANDLE hSerial;
    char port[256];
    int baud_rate;
    int data_bits;
    int parity; // 0=None, 1=Even, 2=Odd
    int stop_bits;
} modbus_serial_win_t;

int modbus_serial_configure_win(modbus_serial_win_t *ctx) {
    DCB dcbSerialParams = {0};
    COMMTIMEOUTS timeouts = {0};
    
    // Open COM port
    ctx->hSerial = CreateFile(ctx->port,
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
    
    if (ctx->hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error opening COM port\n");
        return -1;
    }
    
    // Get current DCB
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(ctx->hSerial, &dcbSerialParams)) {
        fprintf(stderr, "Error getting COM state\n");
        CloseHandle(ctx->hSerial);
        return -1;
    }
    
    // Set parameters
    dcbSerialParams.BaudRate = ctx->baud_rate;
    dcbSerialParams.ByteSize = ctx->data_bits;
    
    switch(ctx->parity) {
        case 0: dcbSerialParams.Parity = NOPARITY; break;
        case 1: dcbSerialParams.Parity = EVENPARITY; break;
        case 2: dcbSerialParams.Parity = ODDPARITY; break;
    }
    
    dcbSerialParams.StopBits = (ctx->stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    
    if (!SetCommState(ctx->hSerial, &dcbSerialParams)) {
        fprintf(stderr, "Error setting COM state\n");
        CloseHandle(ctx->hSerial);
        return -1;
    }
    
    // Set timeouts (milliseconds)
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    if (!SetCommTimeouts(ctx->hSerial, &timeouts)) {
        fprintf(stderr, "Error setting timeouts\n");
        CloseHandle(ctx->hSerial);
        return -1;
    }
    
    return 0;
}
```

---

## Rust Implementation

```rust
use serialport::{SerialPort, DataBits, Parity, StopBits, FlowControl};
use std::time::Duration;
use std::io::{self, Read, Write};

#[derive(Debug, Clone)]
pub struct ModbusSerialConfig {
    pub port: String,
    pub baud_rate: u32,
    pub data_bits: DataBits,
    pub parity: Parity,
    pub stop_bits: StopBits,
    pub timeout: Duration,
}

impl Default for ModbusSerialConfig {
    fn default() -> Self {
        ModbusSerialConfig {
            port: "/dev/ttyUSB0".to_string(),
            baud_rate: 9600,
            data_bits: DataBits::Eight,
            parity: Parity::None,
            stop_bits: StopBits::One,
            timeout: Duration::from_millis(1000),
        }
    }
}

impl ModbusSerialConfig {
    /// Create RTU configuration (typical: 8-N-1)
    pub fn rtu(port: &str, baud_rate: u32) -> Self {
        ModbusSerialConfig {
            port: port.to_string(),
            baud_rate,
            data_bits: DataBits::Eight,
            parity: Parity::None,
            stop_bits: StopBits::One,
            timeout: Duration::from_millis(1000),
        }
    }
    
    /// Create RTU configuration with even parity (8-E-1)
    pub fn rtu_even_parity(port: &str, baud_rate: u32) -> Self {
        ModbusSerialConfig {
            port: port.to_string(),
            baud_rate,
            data_bits: DataBits::Eight,
            parity: Parity::Even,
            stop_bits: StopBits::One,
            timeout: Duration::from_millis(1000),
        }
    }
    
    /// Create ASCII configuration (typical: 7-E-1)
    pub fn ascii(port: &str, baud_rate: u32) -> Self {
        ModbusSerialConfig {
            port: port.to_string(),
            baud_rate,
            data_bits: DataBits::Seven,
            parity: Parity::Even,
            stop_bits: StopBits::One,
            timeout: Duration::from_millis(1000),
        }
    }
    
    /// Open and configure the serial port
    pub fn open(&self) -> io::Result<Box<dyn SerialPort>> {
        let port = serialport::new(&self.port, self.baud_rate)
            .timeout(self.timeout)
            .data_bits(self.data_bits)
            .parity(self.parity)
            .stop_bits(self.stop_bits)
            .flow_control(FlowControl::None)
            .open()
            .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
        
        Ok(port)
    }
}

pub struct ModbusSerial {
    port: Box<dyn SerialPort>,
    config: ModbusSerialConfig,
}

impl ModbusSerial {
    pub fn new(config: ModbusSerialConfig) -> io::Result<Self> {
        let port = config.open()?;
        
        Ok(ModbusSerial {
            port,
            config,
        })
    }
    
    pub fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.port.write(data)
    }
    
    pub fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.port.read(buf)
    }
    
    pub fn flush(&mut self) -> io::Result<()> {
        self.port.flush()
    }
    
    pub fn clear_buffers(&mut self) -> io::Result<()> {
        self.port.clear(serialport::ClearBuffer::All)
            .map_err(|e| io::Error::new(io::ErrorKind::Other, e))
    }
}

// Example usage
fn main() -> io::Result<()> {
    // RTU configuration: 9600 baud, 8-N-1
    let config = ModbusSerialConfig::rtu("/dev/ttyUSB0", 9600);
    
    println!("Opening serial port with configuration:");
    println!("  Port: {}", config.port);
    println!("  Baud: {}", config.baud_rate);
    println!("  Data: {:?}", config.data_bits);
    println!("  Parity: {:?}", config.parity);
    println!("  Stop: {:?}", config.stop_bits);
    
    let mut serial = ModbusSerial::new(config)?;
    
    // Clear any existing data
    serial.clear_buffers()?;
    
    // Example: Send a Modbus request
    let request = [0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD];
    serial.write(&request)?;
    serial.flush()?;
    
    // Example: Read response
    let mut response = [0u8; 256];
    match serial.read(&mut response) {
        Ok(n) => println!("Received {} bytes", n),
        Err(e) => eprintln!("Read error: {}", e),
    }
    
    Ok(())
}

// Advanced: Configuration builder pattern
pub struct SerialConfigBuilder {
    config: ModbusSerialConfig,
}

impl SerialConfigBuilder {
    pub fn new(port: &str) -> Self {
        SerialConfigBuilder {
            config: ModbusSerialConfig {
                port: port.to_string(),
                ..Default::default()
            }
        }
    }
    
    pub fn baud_rate(mut self, baud: u32) -> Self {
        self.config.baud_rate = baud;
        self
    }
    
    pub fn data_bits(mut self, bits: DataBits) -> Self {
        self.config.data_bits = bits;
        self
    }
    
    pub fn parity(mut self, parity: Parity) -> Self {
        self.config.parity = parity;
        self
    }
    
    pub fn stop_bits(mut self, bits: StopBits) -> Self {
        self.config.stop_bits = bits;
        self
    }
    
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.config.timeout = timeout;
        self
    }
    
    pub fn build(self) -> ModbusSerialConfig {
        self.config
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_rtu_config() {
        let config = ModbusSerialConfig::rtu("/dev/ttyUSB0", 19200);
        assert_eq!(config.baud_rate, 19200);
        assert_eq!(config.data_bits, DataBits::Eight);
        assert_eq!(config.parity, Parity::None);
        assert_eq!(config.stop_bits, StopBits::One);
    }
    
    #[test]
    fn test_config_builder() {
        let config = SerialConfigBuilder::new("/dev/ttyS0")
            .baud_rate(115200)
            .parity(Parity::Even)
            .build();
        
        assert_eq!(config.baud_rate, 115200);
        assert_eq!(config.parity, Parity::Even);
    }
}
```

---

## Summary

Serial port configuration is critical for Modbus RTU/ASCII communication success. Key points:

- **Must Match**: All devices on the same serial bus must use identical settings (baud rate, parity, data bits, stop bits)
- **Common RTU**: 8-N-1 (8 data bits, no parity, 1 stop bit) or 8-E-1 with even parity
- **Common ASCII**: 7-E-1 (7 data bits, even parity, 1 stop bit)
- **Baud Rates**: 9600 bps is the most common default, but higher rates (19200, 38400, 115200) are used for faster communication
- **Platform Differences**: Linux/POSIX uses termios API, Windows uses DCB structures
- **Rust Advantages**: The `serialport` crate provides cross-platform abstraction with type-safe configuration

Proper serial configuration ensures reliable frame synchronization, error detection through parity checking, and deterministic timing for character and frame gaps in Modbus RTU protocol. Misconfiguration leads to garbled data, CRC errors, and communication timeouts.