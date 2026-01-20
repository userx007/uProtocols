# TCP Socket Programming for Modbus

## Overview

TCP Socket Programming forms the foundation of Modbus TCP communication, enabling devices to exchange data over Ethernet networks. Unlike traditional Modbus RTU (which uses serial communication), Modbus TCP leverages the reliability and widespread adoption of TCP/IP networks. This topic covers creating both client and server sockets that implement the Modbus TCP protocol.

## Key Concepts

### Modbus TCP Architecture

Modbus TCP uses a **client-server model**:
- **Client (Master)**: Initiates requests to read/write data from slave devices
- **Server (Slave)**: Responds to client requests, providing access to its data tables (coils, discrete inputs, holding registers, input registers)

### Modbus TCP Frame Structure

Unlike Modbus RTU, Modbus TCP uses an MBAP (Modbus Application Protocol) header instead of CRC for error checking:

```
[MBAP Header (7 bytes)] + [Function Code (1 byte)] + [Data (N bytes)]

MBAP Header:
- Transaction ID (2 bytes): For matching requests/responses
- Protocol ID (2 bytes): Always 0x0000 for Modbus
- Length (2 bytes): Number of following bytes
- Unit ID (1 byte): Slave device identifier
```

### Default Port

Modbus TCP typically uses **port 502** (registered with IANA).

## C/C++ Implementation

### C++ Modbus TCP Server

```cpp
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>

#define MODBUS_PORT 502
#define BUFFER_SIZE 260

class ModbusTCPServer {
private:
    int server_fd;
    std::vector<uint16_t> holding_registers;
    std::vector<uint8_t> coils;
    
public:
    ModbusTCPServer(int num_registers = 100, int num_coils = 100) 
        : holding_registers(num_registers, 0), coils(num_coils, 0) {}
    
    bool initialize() {
        // Create socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }
        
        // Allow socket reuse
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Bind to port
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(MODBUS_PORT);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Bind failed" << std::endl;
            return false;
        }
        
        // Listen for connections
        if (listen(server_fd, 5) < 0) {
            std::cerr << "Listen failed" << std::endl;
            return false;
        }
        
        std::cout << "Modbus TCP Server listening on port " << MODBUS_PORT << std::endl;
        return true;
    }
    
    void processRequest(uint8_t* request, int req_len, uint8_t* response, int& resp_len) {
        // Copy MBAP header
        memcpy(response, request, 7);
        
        uint8_t function_code = request[7];
        
        switch (function_code) {
            case 0x03: // Read Holding Registers
                processReadHoldingRegisters(request, req_len, response, resp_len);
                break;
            case 0x06: // Write Single Register
                processWriteSingleRegister(request, req_len, response, resp_len);
                break;
            case 0x10: // Write Multiple Registers
                processWriteMultipleRegisters(request, req_len, response, resp_len);
                break;
            default:
                // Unsupported function - send exception
                response[7] = function_code | 0x80;
                response[8] = 0x01; // Illegal function
                resp_len = 9;
        }
        
        // Update length field in MBAP header
        uint16_t length = resp_len - 6;
        response[4] = (length >> 8) & 0xFF;
        response[5] = length & 0xFF;
    }
    
    void processReadHoldingRegisters(uint8_t* request, int req_len, 
                                     uint8_t* response, int& resp_len) {
        uint16_t start_addr = (request[8] << 8) | request[9];
        uint16_t quantity = (request[10] << 8) | request[11];
        
        response[7] = 0x03; // Function code
        response[8] = quantity * 2; // Byte count
        
        for (int i = 0; i < quantity; i++) {
            uint16_t reg_value = holding_registers[start_addr + i];
            response[9 + i*2] = (reg_value >> 8) & 0xFF;
            response[10 + i*2] = reg_value & 0xFF;
        }
        
        resp_len = 9 + quantity * 2;
    }
    
    void processWriteSingleRegister(uint8_t* request, int req_len, 
                                   uint8_t* response, int& resp_len) {
        uint16_t addr = (request[8] << 8) | request[9];
        uint16_t value = (request[10] << 8) | request[11];
        
        holding_registers[addr] = value;
        
        // Echo request as response
        memcpy(response, request, req_len);
        resp_len = req_len;
    }
    
    void processWriteMultipleRegisters(uint8_t* request, int req_len,
                                      uint8_t* response, int& resp_len) {
        uint16_t start_addr = (request[8] << 8) | request[9];
        uint16_t quantity = (request[10] << 8) | request[11];
        
        for (int i = 0; i < quantity; i++) {
            uint16_t value = (request[13 + i*2] << 8) | request[14 + i*2];
            holding_registers[start_addr + i] = value;
        }
        
        // Response: echo header + start address + quantity
        memcpy(response, request, 12);
        resp_len = 12;
    }
    
    void run() {
        while (true) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                std::cerr << "Accept failed" << std::endl;
                continue;
            }
            
            std::cout << "Client connected: " << inet_ntoa(client_addr.sin_addr) << std::endl;
            
            uint8_t buffer[BUFFER_SIZE];
            uint8_t response[BUFFER_SIZE];
            
            while (true) {
                int bytes_read = recv(client_fd, buffer, BUFFER_SIZE, 0);
                if (bytes_read <= 0) break;
                
                int resp_len = 0;
                processRequest(buffer, bytes_read, response, resp_len);
                send(client_fd, response, resp_len, 0);
            }
            
            close(client_fd);
            std::cout << "Client disconnected" << std::endl;
        }
    }
    
    ~ModbusTCPServer() {
        if (server_fd >= 0) {
            close(server_fd);
        }
    }
};

int main() {
    ModbusTCPServer server;
    if (server.initialize()) {
        server.run();
    }
    return 0;
}
```

### C++ Modbus TCP Client

```cpp
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MODBUS_PORT 502
#define BUFFER_SIZE 260

class ModbusTCPClient {
private:
    int sock_fd;
    uint16_t transaction_id;
    std::string server_ip;
    
public:
    ModbusTCPClient(const std::string& ip) : server_ip(ip), transaction_id(0), sock_fd(-1) {}
    
    bool connect() {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(MODBUS_PORT);
        
        if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address" << std::endl;
            return false;
        }
        
        if (::connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            return false;
        }
        
        std::cout << "Connected to " << server_ip << std::endl;
        return true;
    }
    
    bool readHoldingRegisters(uint8_t unit_id, uint16_t start_addr, 
                             uint16_t quantity, uint16_t* values) {
        uint8_t request[12];
        
        // MBAP Header
        request[0] = (transaction_id >> 8) & 0xFF;
        request[1] = transaction_id & 0xFF;
        transaction_id++;
        
        request[2] = 0x00; // Protocol ID
        request[3] = 0x00;
        request[4] = 0x00; // Length (6 bytes following)
        request[5] = 0x06;
        request[6] = unit_id;
        
        // PDU
        request[7] = 0x03; // Function code
        request[8] = (start_addr >> 8) & 0xFF;
        request[9] = start_addr & 0xFF;
        request[10] = (quantity >> 8) & 0xFF;
        request[11] = quantity & 0xFF;
        
        if (send(sock_fd, request, 12, 0) != 12) {
            std::cerr << "Send failed" << std::endl;
            return false;
        }
        
        uint8_t response[BUFFER_SIZE];
        int bytes_received = recv(sock_fd, response, BUFFER_SIZE, 0);
        
        if (bytes_received < 9) {
            std::cerr << "Invalid response" << std::endl;
            return false;
        }
        
        // Check for exception
        if (response[7] & 0x80) {
            std::cerr << "Modbus exception: " << (int)response[8] << std::endl;
            return false;
        }
        
        // Extract register values
        uint8_t byte_count = response[8];
        for (int i = 0; i < quantity; i++) {
            values[i] = (response[9 + i*2] << 8) | response[10 + i*2];
        }
        
        return true;
    }
    
    bool writeSingleRegister(uint8_t unit_id, uint16_t addr, uint16_t value) {
        uint8_t request[12];
        
        request[0] = (transaction_id >> 8) & 0xFF;
        request[1] = transaction_id & 0xFF;
        transaction_id++;
        
        request[2] = 0x00;
        request[3] = 0x00;
        request[4] = 0x00;
        request[5] = 0x06;
        request[6] = unit_id;
        request[7] = 0x06; // Function code
        request[8] = (addr >> 8) & 0xFF;
        request[9] = addr & 0xFF;
        request[10] = (value >> 8) & 0xFF;
        request[11] = value & 0xFF;
        
        if (send(sock_fd, request, 12, 0) != 12) {
            return false;
        }
        
        uint8_t response[12];
        int bytes_received = recv(sock_fd, response, 12, 0);
        
        return (bytes_received == 12 && !(response[7] & 0x80));
    }
    
    void disconnect() {
        if (sock_fd >= 0) {
            close(sock_fd);
            sock_fd = -1;
        }
    }
    
    ~ModbusTCPClient() {
        disconnect();
    }
};

int main() {
    ModbusTCPClient client("127.0.0.1");
    
    if (client.connect()) {
        // Write a value
        client.writeSingleRegister(1, 0, 1234);
        
        // Read registers
        uint16_t values[10];
        if (client.readHoldingRegisters(1, 0, 10, values)) {
            std::cout << "Register values: ";
            for (int i = 0; i < 10; i++) {
                std::cout << values[i] << " ";
            }
            std::cout << std::endl;
        }
        
        client.disconnect();
    }
    
    return 0;
}
```

## Rust Implementation

### Rust Modbus TCP Server

```rust
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};

const MODBUS_PORT: u16 = 502;

struct ModbusData {
    holding_registers: Vec<u16>,
    coils: Vec<bool>,
}

impl ModbusData {
    fn new(num_registers: usize, num_coils: usize) -> Self {
        ModbusData {
            holding_registers: vec![0; num_registers],
            coils: vec![false; num_coils],
        }
    }
}

struct ModbusTCPServer {
    data: Arc<Mutex<ModbusData>>,
}

impl ModbusTCPServer {
    fn new() -> Self {
        ModbusTCPServer {
            data: Arc::new(Mutex::new(ModbusData::new(100, 100))),
        }
    }
    
    fn process_request(&self, request: &[u8]) -> Vec<u8> {
        if request.len() < 8 {
            return self.create_exception_response(request, 0x01);
        }
        
        let function_code = request[7];
        
        match function_code {
            0x03 => self.read_holding_registers(request),
            0x06 => self.write_single_register(request),
            0x10 => self.write_multiple_registers(request),
            _ => self.create_exception_response(request, 0x01),
        }
    }
    
    fn read_holding_registers(&self, request: &[u8]) -> Vec<u8> {
        let start_addr = ((request[8] as u16) << 8) | (request[9] as u16);
        let quantity = ((request[10] as u16) << 8) | (request[11] as u16);
        
        let mut response = Vec::new();
        
        // Copy MBAP header
        response.extend_from_slice(&request[0..7]);
        
        // Function code
        response.push(0x03);
        
        // Byte count
        response.push((quantity * 2) as u8);
        
        // Register values
        let data = self.data.lock().unwrap();
        for i in 0..quantity {
            let addr = (start_addr + i) as usize;
            if addr < data.holding_registers.len() {
                let value = data.holding_registers[addr];
                response.push((value >> 8) as u8);
                response.push((value & 0xFF) as u8);
            }
        }
        
        // Update length field
        let length = (response.len() - 6) as u16;
        response[4] = (length >> 8) as u8;
        response[5] = (length & 0xFF) as u8;
        
        response
    }
    
    fn write_single_register(&self, request: &[u8]) -> Vec<u8> {
        let addr = ((request[8] as u16) << 8) | (request[9] as u16);
        let value = ((request[10] as u16) << 8) | (request[11] as u16);
        
        let mut data = self.data.lock().unwrap();
        if (addr as usize) < data.holding_registers.len() {
            data.holding_registers[addr as usize] = value;
        }
        
        // Echo request as response
        request.to_vec()
    }
    
    fn write_multiple_registers(&self, request: &[u8]) -> Vec<u8> {
        let start_addr = ((request[8] as u16) << 8) | (request[9] as u16);
        let quantity = ((request[10] as u16) << 8) | (request[11] as u16);
        
        let mut data = self.data.lock().unwrap();
        for i in 0..quantity {
            let addr = (start_addr + i) as usize;
            if addr < data.holding_registers.len() {
                let value = ((request[13 + (i * 2) as usize] as u16) << 8) 
                          | (request[14 + (i * 2) as usize] as u16);
                data.holding_registers[addr] = value;
            }
        }
        
        // Response: first 12 bytes of request
        request[0..12].to_vec()
    }
    
    fn create_exception_response(&self, request: &[u8], exception_code: u8) -> Vec<u8> {
        let mut response = Vec::new();
        response.extend_from_slice(&request[0..7]);
        response.push(request[7] | 0x80); // Function code with error bit
        response.push(exception_code);
        
        let length = 3u16;
        response[4] = (length >> 8) as u8;
        response[5] = (length & 0xFF) as u8;
        
        response
    }
    
    fn handle_client(&self, mut stream: TcpStream) {
        println!("Client connected: {}", stream.peer_addr().unwrap());
        
        let mut buffer = [0u8; 260];
        
        loop {
            match stream.read(&mut buffer) {
                Ok(0) => break, // Connection closed
                Ok(n) => {
                    let response = self.process_request(&buffer[0..n]);
                    if stream.write_all(&response).is_err() {
                        break;
                    }
                }
                Err(_) => break,
            }
        }
        
        println!("Client disconnected");
    }
    
    fn run(&self) -> std::io::Result<()> {
        let listener = TcpListener::bind(format!("0.0.0.0:{}", MODBUS_PORT))?;
        println!("Modbus TCP Server listening on port {}", MODBUS_PORT);
        
        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    let server = ModbusTCPServer {
                        data: Arc::clone(&self.data),
                    };
                    
                    std::thread::spawn(move || {
                        server.handle_client(stream);
                    });
                }
                Err(e) => {
                    eprintln!("Connection failed: {}", e);
                }
            }
        }
        
        Ok(())
    }
}

fn main() -> std::io::Result<()> {
    let server = ModbusTCPServer::new();
    server.run()
}
```

### Rust Modbus TCP Client

```rust
use std::io::{Read, Write};
use std::net::TcpStream;

const MODBUS_PORT: u16 = 502;

struct ModbusTCPClient {
    stream: Option<TcpStream>,
    transaction_id: u16,
}

impl ModbusTCPClient {
    fn new() -> Self {
        ModbusTCPClient {
            stream: None,
            transaction_id: 0,
        }
    }
    
    fn connect(&mut self, server_ip: &str) -> std::io::Result<()> {
        let addr = format!("{}:{}", server_ip, MODBUS_PORT);
        self.stream = Some(TcpStream::connect(addr)?);
        println!("Connected to {}", server_ip);
        Ok(())
    }
    
    fn read_holding_registers(
        &mut self,
        unit_id: u8,
        start_addr: u16,
        quantity: u16,
    ) -> std::io::Result<Vec<u16>> {
        let mut request = Vec::new();
        
        // MBAP Header
        request.push((self.transaction_id >> 8) as u8);
        request.push((self.transaction_id & 0xFF) as u8);
        self.transaction_id = self.transaction_id.wrapping_add(1);
        
        request.push(0x00); // Protocol ID
        request.push(0x00);
        request.push(0x00); // Length
        request.push(0x06);
        request.push(unit_id);
        
        // PDU
        request.push(0x03); // Function code
        request.push((start_addr >> 8) as u8);
        request.push((start_addr & 0xFF) as u8);
        request.push((quantity >> 8) as u8);
        request.push((quantity & 0xFF) as u8);
        
        let stream = self.stream.as_mut().ok_or(std::io::Error::new(
            std::io::ErrorKind::NotConnected,
            "Not connected",
        ))?;
        
        stream.write_all(&request)?;
        
        let mut response = vec![0u8; 260];
        let bytes_read = stream.read(&mut response)?;
        
        if bytes_read < 9 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "Invalid response",
            ));
        }
        
        // Check for exception
        if response[7] & 0x80 != 0 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::Other,
                format!("Modbus exception: {}", response[8]),
            ));
        }
        
        // Extract register values
        let mut values = Vec::new();
        for i in 0..quantity {
            let idx = 9 + (i * 2) as usize;
            let value = ((response[idx] as u16) << 8) | (response[idx + 1] as u16);
            values.push(value);
        }
        
        Ok(values)
    }
    
    fn write_single_register(
        &mut self,
        unit_id: u8,
        addr: u16,
        value: u16,
    ) -> std::io::Result<()> {
        let mut request = Vec::new();
        
        request.push((self.transaction_id >> 8) as u8);
        request.push((self.transaction_id & 0xFF) as u8);
        self.transaction_id = self.transaction_id.wrapping_add(1);
        
        request.push(0x00);
        request.push(0x00);
        request.push(0x00);
        request.push(0x06);
        request.push(unit_id);
        request.push(0x06); // Function code
        request.push((addr >> 8) as u8);
        request.push((addr & 0xFF) as u8);
        request.push((value >> 8) as u8);
        request.push((value & 0xFF) as u8);
        
        let stream = self.stream.as_mut().ok_or(std::io::Error::new(
            std::io::ErrorKind::NotConnected,
            "Not connected",
        ))?;
        
        stream.write_all(&request)?;
        
        let mut response = vec![0u8; 12];
        stream.read_exact(&mut response)?;
        
        if response[7] & 0x80 != 0 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::Other,
                "Write failed",
            ));
        }
        
        Ok(())
    }
    
    fn disconnect(&mut self) {
        self.stream = None;
    }
}

fn main() -> std::io::Result<()> {
    let mut client = ModbusTCPClient::new();
    
    client.connect("127.0.0.1")?;
    
    // Write a value
    client.write_single_register(1, 0, 1234)?;
    
    // Read registers
    let values = client.read_holding_registers(1, 0, 10)?;
    println!("Register values: {:?}", values);
    
    client.disconnect();
    
    Ok(())
}
```

## Summary

TCP Socket Programming for Modbus TCP enables industrial devices to communicate over standard Ethernet networks using the proven Modbus protocol. Key takeaways include:

**Architecture**: Modbus TCP uses a client-server model where clients initiate requests and servers respond with data from their register tables.

**Protocol Structure**: The MBAP header (7 bytes) precedes the standard Modbus PDU, providing transaction tracking, protocol identification, message length, and unit addressing.

**Implementation Considerations**:
- Proper socket lifecycle management (creation, binding, listening, accepting connections)
- Transaction ID tracking for request-response matching
- Error handling for network failures and Modbus exceptions
- Thread safety when multiple clients access shared data (especially in server implementations)
- Support for multiple function codes (0x03 for reading, 0x06 for single writes, 0x10 for multiple writes)

**Language Differences**: C++ offers low-level control with POSIX sockets, while Rust provides memory safety guarantees and modern concurrency primitives through its standard library and type system.

Both implementations demonstrate the fundamental concepts needed to build production-ready Modbus TCP applications, serving as starting points for more sophisticated industrial automation systems.