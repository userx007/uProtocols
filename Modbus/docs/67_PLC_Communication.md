# PLC Communication with Modbus

## Detailed Description

**PLC Communication** refers to the integration and interfacing of Modbus protocol with Programmable Logic Controllers (PLCs), enabling industrial automation systems to exchange data with various field devices, SCADA systems, and other control equipment. PLCs are ruggedized computers designed for industrial control applications, and Modbus serves as one of the most common communication protocols for connecting PLCs to the broader automation infrastructure.

### Key Concepts

#### 1. **PLC Architecture and Modbus Integration**
PLCs typically operate on a scan cycle basis:
- **Input Scan**: Reading physical inputs
- **Program Execution**: Running ladder logic or other control programs
- **Output Scan**: Writing to physical outputs
- **Communication**: Exchanging data via protocols like Modbus

Modbus communication in PLCs can occur in two primary roles:
- **Modbus Master/Client**: PLC initiating requests to remote devices
- **Modbus Slave/Server**: PLC responding to requests from SCADA or other masters

#### 2. **Data Mapping**
PLCs use internal memory areas that map to Modbus address spaces:
- **Coils (0x)**: Discrete outputs → Modbus Function Codes 01, 05, 15
- **Discrete Inputs (1x)**: Digital inputs → Modbus Function Code 02
- **Input Registers (3x)**: Analog inputs → Modbus Function Code 04
- **Holding Registers (4x)**: Analog outputs/data storage → Modbus Function Codes 03, 06, 16

#### 3. **Ladder Logic Integration**
Ladder logic programs can:
- Trigger Modbus read/write operations based on conditions
- Use special function blocks for Modbus communication
- Process Modbus data in real-time within the control logic
- Implement communication error handling and diagnostics

#### 4. **Common Use Cases**
- Remote I/O expansion via Modbus RTU/TCP
- SCADA system integration for monitoring and control
- Inter-PLC communication for distributed control systems
- Integration with VFDs, sensors, meters, and other smart devices
- Data logging and historical trending

---

## C/C++ Code Examples

### Example 1: Modbus RTU Master Implementation for PLC Communication

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

// Modbus Function Codes
#define MODBUS_FC_READ_COILS           0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02
#define MODBUS_FC_READ_HOLDING_REGS    0x03
#define MODBUS_FC_READ_INPUT_REGS      0x04
#define MODBUS_FC_WRITE_SINGLE_COIL    0x05
#define MODBUS_FC_WRITE_SINGLE_REG     0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGS  0x10

// Modbus RTU frame structure
typedef struct {
    uint8_t slave_id;
    uint8_t function_code;
    uint16_t start_address;
    uint16_t quantity;
    uint8_t *data;
    uint16_t crc;
} modbus_frame_t;

// CRC-16 calculation for Modbus RTU
uint16_t modbus_crc16(uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Configure serial port for Modbus RTU
int configure_serial_port(const char *port, int baudrate) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("Unable to open serial port");
        return -1;
    }
    
    struct termios options;
    tcgetattr(fd, &options);
    
    // Set baud rate
    speed_t speed;
    switch (baudrate) {
        case 9600:  speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 115200: speed = B115200; break;
        default: speed = B9600;
    }
    
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    // 8N1 configuration
    options.c_cflag &= ~PARENB;  // No parity
    options.c_cflag &= ~CSTOPB;  // 1 stop bit
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;      // 8 data bits
    options.c_cflag |= (CLOCAL | CREAD);
    
    // Raw mode
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    
    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

// Read holding registers from PLC
int modbus_read_holding_registers(int fd, uint8_t slave_id, 
                                   uint16_t start_addr, uint16_t count, 
                                   uint16_t *registers) {
    uint8_t request[8];
    uint8_t response[256];
    
    // Build request frame
    request[0] = slave_id;
    request[1] = MODBUS_FC_READ_HOLDING_REGS;
    request[2] = (start_addr >> 8) & 0xFF;
    request[3] = start_addr & 0xFF;
    request[4] = (count >> 8) & 0xFF;
    request[5] = count & 0xFF;
    
    // Calculate and append CRC
    uint16_t crc = modbus_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Send request
    write(fd, request, 8);
    
    // Wait for response
    usleep(100000); // 100ms delay
    
    // Read response
    int bytes_read = read(fd, response, sizeof(response));
    if (bytes_read < 5) {
        fprintf(stderr, "Invalid response length\n");
        return -1;
    }
    
    // Verify CRC
    uint16_t received_crc = response[bytes_read - 2] | (response[bytes_read - 1] << 8);
    uint16_t calculated_crc = modbus_crc16(response, bytes_read - 2);
    
    if (received_crc != calculated_crc) {
        fprintf(stderr, "CRC error\n");
        return -1;
    }
    
    // Extract register values
    uint8_t byte_count = response[2];
    for (int i = 0; i < count; i++) {
        registers[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
    }
    
    return 0;
}

// Write multiple registers to PLC
int modbus_write_multiple_registers(int fd, uint8_t slave_id, 
                                     uint16_t start_addr, uint16_t count,
                                     uint16_t *registers) {
    uint8_t request[256];
    uint8_t response[256];
    
    // Build request frame
    request[0] = slave_id;
    request[1] = MODBUS_FC_WRITE_MULTIPLE_REGS;
    request[2] = (start_addr >> 8) & 0xFF;
    request[3] = start_addr & 0xFF;
    request[4] = (count >> 8) & 0xFF;
    request[5] = count & 0xFF;
    request[6] = count * 2; // Byte count
    
    // Add register data
    for (int i = 0; i < count; i++) {
        request[7 + i * 2] = (registers[i] >> 8) & 0xFF;
        request[8 + i * 2] = registers[i] & 0xFF;
    }
    
    int frame_length = 7 + count * 2;
    
    // Calculate and append CRC
    uint16_t crc = modbus_crc16(request, frame_length);
    request[frame_length] = crc & 0xFF;
    request[frame_length + 1] = (crc >> 8) & 0xFF;
    
    // Send request
    write(fd, request, frame_length + 2);
    
    // Wait for response
    usleep(100000);
    
    // Read response
    int bytes_read = read(fd, response, sizeof(response));
    if (bytes_read < 8) {
        fprintf(stderr, "Invalid response\n");
        return -1;
    }
    
    return 0;
}

// Example: PLC communication scenario
int main() {
    const char *port = "/dev/ttyUSB0";
    int fd = configure_serial_port(port, 9600);
    
    if (fd < 0) {
        return 1;
    }
    
    printf("Connected to PLC on %s\n", port);
    
    // Example 1: Read temperature from PLC input registers
    uint16_t temperature_raw;
    if (modbus_read_holding_registers(fd, 1, 1000, 1, &temperature_raw) == 0) {
        float temperature = temperature_raw / 10.0; // Assuming 0.1°C resolution
        printf("Temperature: %.1f°C\n", temperature);
    }
    
    // Example 2: Write setpoint to PLC
    uint16_t setpoint_registers[2];
    setpoint_registers[0] = 250; // 25.0°C setpoint
    setpoint_registers[1] = 1;   // Enable control
    
    if (modbus_write_multiple_registers(fd, 1, 2000, 2, setpoint_registers) == 0) {
        printf("Setpoint written successfully\n");
    }
    
    // Example 3: Read multiple sensor values
    uint16_t sensor_data[10];
    if (modbus_read_holding_registers(fd, 1, 3000, 10, sensor_data) == 0) {
        printf("Sensor readings:\n");
        for (int i = 0; i < 10; i++) {
            printf("  Sensor %d: %u\n", i, sensor_data[i]);
        }
    }
    
    close(fd);
    return 0;
}
```

### Example 2: Modbus TCP Client for PLC Integration

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

class ModbusTCPClient {
private:
    int sockfd;
    std::string plc_ip;
    int plc_port;
    uint16_t transaction_id;
    
public:
    ModbusTCPClient(const std::string& ip, int port = 502) 
        : plc_ip(ip), plc_port(port), transaction_id(0) {
        sockfd = -1;
    }
    
    ~ModbusTCPClient() {
        disconnect();
    }
    
    bool connect() {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(plc_port);
        
        if (inet_pton(AF_INET, plc_ip.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address" << std::endl;
            return false;
        }
        
        if (::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            return false;
        }
        
        std::cout << "Connected to PLC at " << plc_ip << ":" << plc_port << std::endl;
        return true;
    }
    
    void disconnect() {
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
    }
    
    bool readHoldingRegisters(uint8_t unit_id, uint16_t start_addr, 
                              uint16_t count, std::vector<uint16_t>& values) {
        uint8_t request[12];
        
        // MBAP Header
        request[0] = (transaction_id >> 8) & 0xFF;
        request[1] = transaction_id & 0xFF;
        request[2] = 0x00; // Protocol identifier
        request[3] = 0x00;
        request[4] = 0x00; // Length (6 bytes following)
        request[5] = 0x06;
        request[6] = unit_id;
        
        // PDU
        request[7] = 0x03; // Function code
        request[8] = (start_addr >> 8) & 0xFF;
        request[9] = start_addr & 0xFF;
        request[10] = (count >> 8) & 0xFF;
        request[11] = count & 0xFF;
        
        transaction_id++;
        
        // Send request
        if (send(sockfd, request, 12, 0) != 12) {
            std::cerr << "Send failed" << std::endl;
            return false;
        }
        
        // Receive response
        uint8_t response[256];
        int bytes_received = recv(sockfd, response, sizeof(response), 0);
        
        if (bytes_received < 9) {
            std::cerr << "Invalid response" << std::endl;
            return false;
        }
        
        // Parse response
        uint8_t byte_count = response[8];
        values.clear();
        
        for (int i = 0; i < count; i++) {
            uint16_t value = (response[9 + i * 2] << 8) | response[10 + i * 2];
            values.push_back(value);
        }
        
        return true;
    }
    
    bool writeMultipleRegisters(uint8_t unit_id, uint16_t start_addr,
                                const std::vector<uint16_t>& values) {
        size_t count = values.size();
        std::vector<uint8_t> request(13 + count * 2);
        
        // MBAP Header
        request[0] = (transaction_id >> 8) & 0xFF;
        request[1] = transaction_id & 0xFF;
        request[2] = 0x00;
        request[3] = 0x00;
        uint16_t length = 7 + count * 2;
        request[4] = (length >> 8) & 0xFF;
        request[5] = length & 0xFF;
        request[6] = unit_id;
        
        // PDU
        request[7] = 0x10; // Function code
        request[8] = (start_addr >> 8) & 0xFF;
        request[9] = start_addr & 0xFF;
        request[10] = (count >> 8) & 0xFF;
        request[11] = count & 0xFF;
        request[12] = count * 2; // Byte count
        
        // Register values
        for (size_t i = 0; i < count; i++) {
            request[13 + i * 2] = (values[i] >> 8) & 0xFF;
            request[14 + i * 2] = values[i] & 0xFF;
        }
        
        transaction_id++;
        
        // Send request
        if (send(sockfd, request.data(), request.size(), 0) != (ssize_t)request.size()) {
            std::cerr << "Send failed" << std::endl;
            return false;
        }
        
        // Receive response
        uint8_t response[12];
        recv(sockfd, response, sizeof(response), 0);
        
        return true;
    }
};

// Example usage
int main() {
    ModbusTCPClient plc("192.168.1.100", 502);
    
    if (!plc.connect()) {
        return 1;
    }
    
    // Read process values from PLC
    std::vector<uint16_t> process_values;
    if (plc.readHoldingRegisters(1, 0, 10, process_values)) {
        std::cout << "Process values:" << std::endl;
        for (size_t i = 0; i < process_values.size(); i++) {
            std::cout << "  Register " << i << ": " << process_values[i] << std::endl;
        }
    }
    
    // Write control parameters to PLC
    std::vector<uint16_t> control_params = {1000, 2000, 500};
    if (plc.writeMultipleRegisters(1, 100, control_params)) {
        std::cout << "Control parameters written successfully" << std::endl;
    }
    
    return 0;
}
```

---

## Rust Code Examples

### Example 1: Modbus RTU Master in Rust

```rust
use std::io::{self, Read, Write};
use std::time::Duration;
use serialport::{SerialPort, DataBits, StopBits, Parity};

// Modbus function codes
const FC_READ_HOLDING_REGISTERS: u8 = 0x03;
const FC_WRITE_MULTIPLE_REGISTERS: u8 = 0x10;

// Calculate Modbus CRC-16
fn calculate_crc(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in data {
        crc ^= *byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

struct ModbusRTUMaster {
    port: Box<dyn SerialPort>,
}

impl ModbusRTUMaster {
    fn new(port_name: &str, baud_rate: u32) -> io::Result<Self> {
        let port = serialport::new(port_name, baud_rate)
            .timeout(Duration::from_millis(1000))
            .data_bits(DataBits::Eight)
            .stop_bits(StopBits::One)
            .parity(Parity::None)
            .open()?;
        
        Ok(ModbusRTUMaster { port })
    }
    
    fn read_holding_registers(
        &mut self,
        slave_id: u8,
        start_addr: u16,
        count: u16,
    ) -> io::Result<Vec<u16>> {
        let mut request = Vec::new();
        
        // Build request
        request.push(slave_id);
        request.push(FC_READ_HOLDING_REGISTERS);
        request.extend_from_slice(&start_addr.to_be_bytes());
        request.extend_from_slice(&count.to_be_bytes());
        
        // Add CRC
        let crc = calculate_crc(&request);
        request.extend_from_slice(&crc.to_le_bytes());
        
        // Send request
        self.port.write_all(&request)?;
        self.port.flush()?;
        
        // Read response
        std::thread::sleep(Duration::from_millis(100));
        let mut response = vec![0u8; 256];
        let bytes_read = self.port.read(&mut response)?;
        response.truncate(bytes_read);
        
        // Verify CRC
        if bytes_read < 5 {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "Response too short"));
        }
        
        let received_crc = u16::from_le_bytes([
            response[bytes_read - 2],
            response[bytes_read - 1],
        ]);
        let calculated_crc = calculate_crc(&response[..bytes_read - 2]);
        
        if received_crc != calculated_crc {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "CRC mismatch"));
        }
        
        // Parse register values
        let byte_count = response[2] as usize;
        let mut registers = Vec::new();
        
        for i in 0..count as usize {
            let reg_value = u16::from_be_bytes([
                response[3 + i * 2],
                response[4 + i * 2],
            ]);
            registers.push(reg_value);
        }
        
        Ok(registers)
    }
    
    fn write_multiple_registers(
        &mut self,
        slave_id: u8,
        start_addr: u16,
        values: &[u16],
    ) -> io::Result<()> {
        let mut request = Vec::new();
        let count = values.len() as u16;
        
        // Build request
        request.push(slave_id);
        request.push(FC_WRITE_MULTIPLE_REGISTERS);
        request.extend_from_slice(&start_addr.to_be_bytes());
        request.extend_from_slice(&count.to_be_bytes());
        request.push((count * 2) as u8); // Byte count
        
        // Add register values
        for value in values {
            request.extend_from_slice(&value.to_be_bytes());
        }
        
        // Add CRC
        let crc = calculate_crc(&request);
        request.extend_from_slice(&crc.to_le_bytes());
        
        // Send request
        self.port.write_all(&request)?;
        self.port.flush()?;
        
        // Read response
        std::thread::sleep(Duration::from_millis(100));
        let mut response = vec![0u8; 256];
        self.port.read(&mut response)?;
        
        Ok(())
    }
}

fn main() -> io::Result<()> {
    let mut plc = ModbusRTUMaster::new("/dev/ttyUSB0", 9600)?;
    println!("Connected to PLC");
    
    // Read temperature sensors from PLC
    match plc.read_holding_registers(1, 1000, 5) {
        Ok(temps) => {
            println!("Temperature readings:");
            for (i, temp) in temps.iter().enumerate() {
                println!("  Sensor {}: {:.1}°C", i, *temp as f32 / 10.0);
            }
        }
        Err(e) => eprintln!("Error reading temperatures: {}", e),
    }
    
    // Write setpoints to PLC
    let setpoints = vec![250, 300, 280]; // 25.0°C, 30.0°C, 28.0°C
    match plc.write_multiple_registers(1, 2000, &setpoints) {
        Ok(_) => println!("Setpoints written successfully"),
        Err(e) => eprintln!("Error writing setpoints: {}", e),
    }
    
    Ok(())
}
```

### Example 2: Async Modbus TCP Client for PLC Communication

```rust
use tokio::net::TcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::io;

struct ModbusTCPClient {
    stream: TcpStream,
    transaction_id: u16,
}

impl ModbusTCPClient {
    async fn connect(addr: &str) -> io::Result<Self> {
        let stream = TcpStream::connect(addr).await?;
        println!("Connected to PLC at {}", addr);
        
        Ok(ModbusTCPClient {
            stream,
            transaction_id: 0,
        })
    }
    
    async fn read_holding_registers(
        &mut self,
        unit_id: u8,
        start_addr: u16,
        count: u16,
    ) -> io::Result<Vec<u16>> {
        let mut request = Vec::new();
        
        // MBAP Header
        request.extend_from_slice(&self.transaction_id.to_be_bytes());
        request.extend_from_slice(&[0x00, 0x00]); // Protocol ID
        request.extend_from_slice(&[0x00, 0x06]); // Length
        request.push(unit_id);
        
        // PDU
        request.push(0x03); // Function code
        request.extend_from_slice(&start_addr.to_be_bytes());
        request.extend_from_slice(&count.to_be_bytes());
        
        self.transaction_id = self.transaction_id.wrapping_add(1);
        
        // Send request
        self.stream.write_all(&request).await?;
        
        // Read response
        let mut response = vec![0u8; 256];
        let bytes_read = self.stream.read(&mut response).await?;
        
        if bytes_read < 9 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Invalid response length",
            ));
        }
        
        // Parse registers
        let mut registers = Vec::new();
        for i in 0..count as usize {
            let reg = u16::from_be_bytes([
                response[9 + i * 2],
                response[10 + i * 2],
            ]);
            registers.push(reg);
        }
        
        Ok(registers)
    }
    
    async fn write_multiple_registers(
        &mut self,
        unit_id: u8,
        start_addr: u16,
        values: &[u16],
    ) -> io::Result<()> {
        let count = values.len() as u16;
        let length = 7 + count * 2;
        let mut request = Vec::new();
        
        // MBAP Header
        request.extend_from_slice(&self.transaction_id.to_be_bytes());
        request.extend_from_slice(&[0x00, 0x00]); // Protocol ID
        request.extend_from_slice(&length.to_be_bytes());
        request.push(unit_id);
        
        // PDU
        request.push(0x10); // Function code
        request.extend_from_slice(&start_addr.to_be_bytes());
        request.extend_from_slice(&count.to_be_bytes());
        request.push((count * 2) as u8); // Byte count
        
        // Register values
        for value in values {
            request.extend_from_slice(&value.to_be_bytes());
        }
        
        self.transaction_id = self.transaction_id.wrapping_add(1);
        
        // Send request
        self.stream.write_all(&request).await?;
        
        // Read response
        let mut response = vec![0u8; 12];
        self.stream.read(&mut response).await?;
        
        Ok(())
    }
}

#[tokio::main]
async fn main() -> io::Result<()> {
    let mut plc = ModbusTCPClient::connect("192.168.1.100:502").await?;
    
    // Monitor PLC process values in a loop
    for cycle in 0..10 {
        println!("\n=== Scan Cycle {} ===", cycle);
        
        // Read process values
        match plc.read_holding_registers(1, 0, 8).await {
            Ok(values) => {
                println!("Process Variables:");
                println!("  Motor Speed: {} RPM", values[0]);
                println!("  Temperature: {:.1}°C", values[1] as f32 / 10.0);
                println!("  Pressure: {} PSI", values[2]);
                println!("  Flow Rate: {} L/min", values[3]);
            }
            Err(e) => eprintln!("Read error: {}", e),
        }
        
        // Write control outputs based on logic
        let outputs = vec![1, 500, 0]; // Start, Speed setpoint, Stop
        if let Err(e) = plc.write_multiple_registers(1, 100, &outputs).await {
            eprintln!("Write error: {}", e);
        }
        
        tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
    }
    
    Ok(())
}
```

---

## Summary

**PLC Communication with Modbus** enables seamless integration between programmable logic controllers and industrial automation systems. Key takeaways include:

1. **Protocol Flexibility**: Modbus RTU (serial) and Modbus TCP (Ethernet) provide options for different network architectures and industrial environments.

2. **Data Mapping**: Understanding the relationship between PLC memory areas (coils, discrete inputs, input registers, holding registers) and Modbus address spaces is crucial for effective communication.

3. **Real-time Control**: PLCs can act as both Modbus masters (initiating requests) and slaves (responding to SCADA/HMI), enabling distributed control architectures.

4. **Programming Integration**: Modern implementations in C/C++ and Rust provide robust, type-safe communication libraries with proper error handling and CRC validation.

5. **Industrial Applications**: Common uses include remote I/O expansion, SCADA integration, inter-PLC communication, and integration with smart field devices like VFDs and sensors.

6. **Ladder Logic Coordination**: Modbus communication can be triggered by ladder logic conditions, allowing control programs to dynamically read/write data based on process requirements.

The code examples demonstrate practical implementations for both RTU and TCP variants, with features like CRC validation, timeout handling, and asynchronous communication patterns suitable for production industrial environments.