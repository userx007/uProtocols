# Virtual COM Port Tunneling

## Detailed Description

Virtual COM Port Tunneling is a technique that creates software-based virtual serial ports (COM ports) that tunnel serial communication data over TCP/IP networks. This approach enables legacy applications and devices that were designed to communicate via physical RS-232/RS-485 serial ports to work seamlessly over modern network infrastructure without requiring code modifications.

### How It Works

The system operates by creating a pair of endpoints:

1. **Virtual COM Port Driver**: A kernel-level or user-space driver that presents a virtual serial port to the operating system (e.g., COM3, COM4 on Windows, or /dev/ttyV0 on Linux)
2. **Network Tunnel**: A TCP/IP connection that carries the serial data between endpoints

When a legacy application writes data to the virtual COM port, the driver intercepts this data and forwards it through a TCP/IP connection to a remote endpoint. The remote endpoint can be:
- A physical serial device connected to a serial-to-Ethernet converter
- Another virtual COM port on a different machine
- A Modbus TCP gateway bridging to Modbus RTU devices

### Common Use Cases

- **Legacy SCADA Systems**: Connecting old supervisory control systems to modern networked PLCs
- **Industrial Automation**: Enabling serial-based HMI software to communicate with remote Modbus devices
- **Equipment Monitoring**: Accessing serial-connected instruments over corporate networks
- **Remote Diagnostics**: Allowing technicians to connect to field devices from central locations
- **Modbus RTU to Modbus TCP Bridging**: Making RTU devices accessible via TCP/IP

### Key Advantages

- **No Application Changes**: Legacy software continues working without modifications
- **Network Scalability**: Extend serial communications beyond traditional cable length limitations
- **Centralized Management**: Monitor and log all serial communications
- **Cost Effective**: Eliminates need for physical serial port expansion cards
- **Flexibility**: Easy reconfiguration of port assignments and routing

## Programming Examples

### C/C++ Implementation

Here's a cross-platform virtual COM port tunneling implementation:

```c
// vcom_tunnel.h
#ifndef VCOM_TUNNEL_H
#define VCOM_TUNNEL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE serial_handle_t;
    typedef SOCKET socket_handle_t;
#else
    #include <termios.h>
    typedef int serial_handle_t;
    typedef int socket_handle_t;
#endif

#define BUFFER_SIZE 4096
#define DEFAULT_TIMEOUT 1000

typedef struct {
    serial_handle_t serial_port;
    socket_handle_t network_socket;
    char port_name[64];
    char remote_host[256];
    uint16_t remote_port;
    uint32_t baud_rate;
    bool running;
    pthread_t serial_to_net_thread;
    pthread_t net_to_serial_thread;
} vcom_tunnel_t;

// Function declarations
vcom_tunnel_t* vcom_tunnel_create(const char* port_name, 
                                   const char* remote_host,
                                   uint16_t remote_port,
                                   uint32_t baud_rate);
int vcom_tunnel_start(vcom_tunnel_t* tunnel);
void vcom_tunnel_stop(vcom_tunnel_t* tunnel);
void vcom_tunnel_destroy(vcom_tunnel_t* tunnel);

#endif // VCOM_TUNNEL_H
```

```c
// vcom_tunnel.c
#include "vcom_tunnel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#ifndef _WIN32
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
#endif

// Open serial port with specified parameters
static serial_handle_t open_serial_port(const char* port_name, uint32_t baud_rate) {
#ifdef _WIN32
    HANDLE hSerial = CreateFile(port_name,
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);
    
    if (hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error opening serial port %s\n", port_name);
        return INVALID_HANDLE_VALUE;
    }
    
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }
    
    dcbSerialParams.BaudRate = baud_rate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }
    
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    SetCommTimeouts(hSerial, &timeouts);
    
    return hSerial;
#else
    int fd = open(port_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("Error opening serial port");
        return -1;
    }
    
    struct termios options;
    tcgetattr(fd, &options);
    
    speed_t speed;
    switch(baud_rate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: speed = B9600;
    }
    
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;
    
    tcsetattr(fd, TCSANOW, &options);
    
    return fd;
#endif
}

// Connect to remote TCP server
static socket_handle_t connect_tcp(const char* host, uint16_t port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    struct sockaddr_in server_addr;
    socket_handle_t sock = socket(AF_INET, SOCK_STREAM, 0);
    
#ifdef _WIN32
    if (sock == INVALID_SOCKET) {
#else
    if (sock < 0) {
#endif
        perror("Socket creation failed");
        return -1;
    }
    
    struct hostent* server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "Host resolution failed\n");
        return -1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    
    printf("Connected to %s:%d\n", host, port);
    return sock;
}

// Thread to forward data from serial to network
static void* serial_to_network_thread(void* arg) {
    vcom_tunnel_t* tunnel = (vcom_tunnel_t*)arg;
    uint8_t buffer[BUFFER_SIZE];
    
    while (tunnel->running) {
        int bytes_read = 0;
        
#ifdef _WIN32
        DWORD dwRead;
        if (ReadFile(tunnel->serial_port, buffer, BUFFER_SIZE, &dwRead, NULL)) {
            bytes_read = dwRead;
        }
#else
        bytes_read = read(tunnel->serial_port, buffer, BUFFER_SIZE);
#endif
        
        if (bytes_read > 0) {
            printf("Serial->Net: %d bytes\n", bytes_read);
            
#ifdef _WIN32
            send(tunnel->network_socket, (char*)buffer, bytes_read, 0);
#else
            write(tunnel->network_socket, buffer, bytes_read);
#endif
        }
    }
    
    return NULL;
}

// Thread to forward data from network to serial
static void* network_to_serial_thread(void* arg) {
    vcom_tunnel_t* tunnel = (vcom_tunnel_t*)arg;
    uint8_t buffer[BUFFER_SIZE];
    
    while (tunnel->running) {
        int bytes_read = 0;
        
#ifdef _WIN32
        bytes_read = recv(tunnel->network_socket, (char*)buffer, BUFFER_SIZE, 0);
#else
        bytes_read = read(tunnel->network_socket, buffer, BUFFER_SIZE);
#endif
        
        if (bytes_read > 0) {
            printf("Net->Serial: %d bytes\n", bytes_read);
            
#ifdef _WIN32
            DWORD dwWritten;
            WriteFile(tunnel->serial_port, buffer, bytes_read, &dwWritten, NULL);
#else
            write(tunnel->serial_port, buffer, bytes_read);
#endif
        } else if (bytes_read == 0) {
            printf("Connection closed by remote\n");
            tunnel->running = false;
            break;
        }
    }
    
    return NULL;
}

vcom_tunnel_t* vcom_tunnel_create(const char* port_name, 
                                   const char* remote_host,
                                   uint16_t remote_port,
                                   uint32_t baud_rate) {
    vcom_tunnel_t* tunnel = (vcom_tunnel_t*)malloc(sizeof(vcom_tunnel_t));
    if (!tunnel) return NULL;
    
    memset(tunnel, 0, sizeof(vcom_tunnel_t));
    strncpy(tunnel->port_name, port_name, sizeof(tunnel->port_name) - 1);
    strncpy(tunnel->remote_host, remote_host, sizeof(tunnel->remote_host) - 1);
    tunnel->remote_port = remote_port;
    tunnel->baud_rate = baud_rate;
    tunnel->running = false;
    
    return tunnel;
}

int vcom_tunnel_start(vcom_tunnel_t* tunnel) {
    if (!tunnel) return -1;
    
    // Open serial port
    tunnel->serial_port = open_serial_port(tunnel->port_name, tunnel->baud_rate);
#ifdef _WIN32
    if (tunnel->serial_port == INVALID_HANDLE_VALUE) {
#else
    if (tunnel->serial_port < 0) {
#endif
        fprintf(stderr, "Failed to open serial port\n");
        return -1;
    }
    
    // Connect to network
    tunnel->network_socket = connect_tcp(tunnel->remote_host, tunnel->remote_port);
    if (tunnel->network_socket < 0) {
        fprintf(stderr, "Failed to connect to network\n");
#ifdef _WIN32
        CloseHandle(tunnel->serial_port);
#else
        close(tunnel->serial_port);
#endif
        return -1;
    }
    
    tunnel->running = true;
    
    // Start forwarding threads
    pthread_create(&tunnel->serial_to_net_thread, NULL, serial_to_network_thread, tunnel);
    pthread_create(&tunnel->net_to_serial_thread, NULL, network_to_serial_thread, tunnel);
    
    printf("Tunnel started: %s <-> %s:%d\n", 
           tunnel->port_name, tunnel->remote_host, tunnel->remote_port);
    
    return 0;
}

void vcom_tunnel_stop(vcom_tunnel_t* tunnel) {
    if (!tunnel) return;
    
    tunnel->running = false;
    
    pthread_join(tunnel->serial_to_net_thread, NULL);
    pthread_join(tunnel->net_to_serial_thread, NULL);
    
#ifdef _WIN32
    if (tunnel->serial_port != INVALID_HANDLE_VALUE) {
        CloseHandle(tunnel->serial_port);
    }
    if (tunnel->network_socket != INVALID_SOCKET) {
        closesocket(tunnel->network_socket);
    }
#else
    if (tunnel->serial_port >= 0) {
        close(tunnel->serial_port);
    }
    if (tunnel->network_socket >= 0) {
        close(tunnel->network_socket);
    }
#endif
    
    printf("Tunnel stopped\n");
}

void vcom_tunnel_destroy(vcom_tunnel_t* tunnel) {
    if (!tunnel) return;
    
    vcom_tunnel_stop(tunnel);
    free(tunnel);
}
```

```c
// Example usage
// main.c
#include "vcom_tunnel.h"
#include <signal.h>

static vcom_tunnel_t* g_tunnel = NULL;

void signal_handler(int sig) {
    if (g_tunnel) {
        vcom_tunnel_stop(g_tunnel);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        printf("Usage: %s <serial_port> <remote_host> <remote_port>\n", argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    g_tunnel = vcom_tunnel_create(argv[1], argv[2], atoi(argv[3]), 9600);
    
    if (vcom_tunnel_start(g_tunnel) != 0) {
        fprintf(stderr, "Failed to start tunnel\n");
        vcom_tunnel_destroy(g_tunnel);
        return 1;
    }
    
    // Keep running until signal
    while (g_tunnel->running) {
        sleep(1);
    }
    
    vcom_tunnel_destroy(g_tunnel);
    return 0;
}
```

### Rust Implementation

Here's a modern async Rust implementation using tokio:

```rust
// Cargo.toml
// [dependencies]
// tokio = { version = "1.35", features = ["full"] }
// tokio-serial = "5.4"
// bytes = "1.5"
// anyhow = "1.0"

use anyhow::{Context, Result};
use bytes::BytesMut;
use std::io;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::mpsc;
use tokio_serial::{SerialPort, SerialPortBuilderExt, SerialStream};

const BUFFER_SIZE: usize = 4096;

pub struct VirtualComTunnel {
    port_name: String,
    remote_addr: String,
    baud_rate: u32,
}

impl VirtualComTunnel {
    pub fn new(port_name: String, remote_addr: String, baud_rate: u32) -> Self {
        Self {
            port_name,
            remote_addr,
            baud_rate,
        }
    }

    pub async fn run(&self) -> Result<()> {
        // Open serial port
        let mut serial = tokio_serial::new(&self.port_name, self.baud_rate)
            .open_native_async()
            .context("Failed to open serial port")?;

        println!("Serial port {} opened at {} baud", self.port_name, self.baud_rate);

        // Connect to TCP server
        let tcp_stream = TcpStream::connect(&self.remote_addr)
            .await
            .context("Failed to connect to remote server")?;

        println!("Connected to {}", self.remote_addr);

        // Split streams for bidirectional communication
        let (tcp_read, tcp_write) = tcp_stream.into_split();
        let (serial_read, serial_write) = tokio::io::split(&mut serial);

        // Create channels for graceful shutdown
        let (tx_stop, mut rx_stop) = mpsc::channel::<()>(1);
        let tx_stop_clone = tx_stop.clone();

        // Spawn task for serial -> network
        let serial_to_net = tokio::spawn(async move {
            if let Err(e) = forward_serial_to_network(serial_read, tcp_write).await {
                eprintln!("Serial->Network error: {}", e);
            }
            let _ = tx_stop.send(()).await;
        });

        // Spawn task for network -> serial
        let net_to_serial = tokio::spawn(async move {
            if let Err(e) = forward_network_to_serial(tcp_read, serial_write).await {
                eprintln!("Network->Serial error: {}", e);
            }
            let _ = tx_stop_clone.send(()).await;
        });

        // Wait for either task to complete or signal shutdown
        tokio::select! {
            _ = serial_to_net => println!("Serial->Network task completed"),
            _ = net_to_serial => println!("Network->Serial task completed"),
            _ = rx_stop.recv() => println!("Shutdown signal received"),
            _ = tokio::signal::ctrl_c() => println!("Ctrl+C received"),
        }

        println!("Tunnel shutting down...");
        Ok(())
    }
}

async fn forward_serial_to_network<R, W>(
    mut serial_read: R,
    mut tcp_write: W,
) -> Result<()>
where
    R: AsyncReadExt + Unpin,
    W: AsyncWriteExt + Unpin,
{
    let mut buffer = BytesMut::with_capacity(BUFFER_SIZE);
    
    loop {
        buffer.clear();
        buffer.resize(BUFFER_SIZE, 0);
        
        match serial_read.read(&mut buffer).await {
            Ok(0) => {
                println!("Serial port closed");
                break;
            }
            Ok(n) => {
                println!("Serial->Net: {} bytes", n);
                tcp_write.write_all(&buffer[..n]).await?;
                tcp_write.flush().await?;
            }
            Err(e) if e.kind() == io::ErrorKind::TimedOut => {
                continue;
            }
            Err(e) => {
                return Err(e.into());
            }
        }
    }
    
    Ok(())
}

async fn forward_network_to_serial<R, W>(
    mut tcp_read: R,
    mut serial_write: W,
) -> Result<()>
where
    R: AsyncReadExt + Unpin,
    W: AsyncWriteExt + Unpin,
{
    let mut buffer = BytesMut::with_capacity(BUFFER_SIZE);
    
    loop {
        buffer.clear();
        buffer.resize(BUFFER_SIZE, 0);
        
        match tcp_read.read(&mut buffer).await {
            Ok(0) => {
                println!("Network connection closed");
                break;
            }
            Ok(n) => {
                println!("Net->Serial: {} bytes", n);
                serial_write.write_all(&buffer[..n]).await?;
                serial_write.flush().await?;
            }
            Err(e) => {
                return Err(e.into());
            }
        }
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() != 4 {
        eprintln!("Usage: {} <serial_port> <remote_addr> <baud_rate>", args[0]);
        eprintln!("Example: {} /dev/ttyUSB0 192.168.1.100:502 9600", args[0]);
        std::process::exit(1);
    }
    
    let port_name = args[1].clone();
    let remote_addr = args[2].clone();
    let baud_rate: u32 = args[3].parse().context("Invalid baud rate")?;
    
    let tunnel = VirtualComTunnel::new(port_name, remote_addr, baud_rate);
    
    tunnel.run().await?;
    
    Ok(())
}
```

Here's an advanced example with Modbus RTU to Modbus TCP bridging:

```rust
// modbus_bridge.rs
use anyhow::{Context, Result};
use bytes::{Buf, BufMut, BytesMut};
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio_serial::SerialStream;

const MODBUS_TCP_HEADER_LEN: usize = 6;
const MODBUS_RTU_MIN_LEN: usize = 4;

pub struct ModbusBridge {
    serial_port: String,
    baud_rate: u32,
    tcp_bind_addr: String,
}

impl ModbusBridge {
    pub fn new(serial_port: String, baud_rate: u32, tcp_bind_addr: String) -> Self {
        Self {
            serial_port,
            baud_rate,
            tcp_bind_addr,
        }
    }

    pub async fn run(&self) -> Result<()> {
        let listener = TcpListener::bind(&self.tcp_bind_addr)
            .await
            .context("Failed to bind TCP listener")?;

        println!("Modbus TCP/RTU bridge listening on {}", self.tcp_bind_addr);
        println!("Serial port: {} @ {} baud", self.serial_port, self.baud_rate);

        loop {
            let (socket, addr) = listener.accept().await?;
            println!("New connection from {}", addr);

            let serial_port = self.serial_port.clone();
            let baud_rate = self.baud_rate;

            tokio::spawn(async move {
                if let Err(e) = handle_client(socket, serial_port, baud_rate).await {
                    eprintln!("Client handler error: {}", e);
                }
            });
        }
    }
}

async fn handle_client(
    mut socket: TcpStream,
    serial_port: String,
    baud_rate: u32,
) -> Result<()> {
    let mut serial = tokio_serial::new(&serial_port, baud_rate)
        .timeout(Duration::from_millis(1000))
        .open_native_async()
        .context("Failed to open serial port")?;

    let mut tcp_buffer = BytesMut::with_capacity(260);
    let mut serial_buffer = BytesMut::with_capacity(260);

    loop {
        // Read Modbus TCP request
        tcp_buffer.clear();
        tcp_buffer.resize(MODBUS_TCP_HEADER_LEN, 0);
        
        socket.read_exact(&mut tcp_buffer).await?;
        
        let transaction_id = tcp_buffer.get_u16();
        let protocol_id = tcp_buffer.get_u16();
        let length = tcp_buffer.get_u16() as usize;
        
        if protocol_id != 0 {
            eprintln!("Invalid protocol ID: {}", protocol_id);
            continue;
        }
        
        // Read PDU
        tcp_buffer.resize(length, 0);
        socket.read_exact(&mut tcp_buffer).await?;
        
        let slave_id = tcp_buffer[0];
        let pdu = &tcp_buffer[1..];
        
        println!("TCP->RTU: TxID={}, Slave={}, Len={}", 
                 transaction_id, slave_id, pdu.len());
        
        // Build RTU frame (Slave ID + PDU + CRC)
        serial_buffer.clear();
        serial_buffer.put_u8(slave_id);
        serial_buffer.put_slice(pdu);
        
        let crc = calculate_crc16(&serial_buffer);
        serial_buffer.put_u16_le(crc);
        
        // Send to serial
        serial.write_all(&serial_buffer).await?;
        serial.flush().await?;
        
        // Read RTU response
        serial_buffer.clear();
        tokio::time::sleep(Duration::from_millis(50)).await;
        
        let n = serial.read_buf(&mut serial_buffer).await?;
        
        if n < MODBUS_RTU_MIN_LEN {
            eprintln!("Invalid RTU response length: {}", n);
            continue;
        }
        
        // Verify CRC
        let frame_len = serial_buffer.len();
        let received_crc = u16::from_le_bytes([
            serial_buffer[frame_len - 2],
            serial_buffer[frame_len - 1],
        ]);
        
        let calculated_crc = calculate_crc16(&serial_buffer[..frame_len - 2]);
        
        if received_crc != calculated_crc {
            eprintln!("CRC mismatch: received={:04X}, calculated={:04X}", 
                     received_crc, calculated_crc);
            continue;
        }
        
        // Build TCP response
        let response_pdu = &serial_buffer[1..frame_len - 2];
        
        println!("RTU->TCP: TxID={}, Len={}", transaction_id, response_pdu.len());
        
        tcp_buffer.clear();
        tcp_buffer.put_u16(transaction_id);
        tcp_buffer.put_u16(0); // Protocol ID
        tcp_buffer.put_u16((response_pdu.len() + 1) as u16);
        tcp_buffer.put_u8(slave_id);
        tcp_buffer.put_slice(response_pdu);
        
        socket.write_all(&tcp_buffer).await?;
        socket.flush().await?;
    }
}

fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for &byte in data {
        crc ^= byte as u16;
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
```

## Summary

Virtual COM Port Tunneling is essential for integrating legacy serial-based industrial systems with modern TCP/IP networks. It provides transparent serial communication over networks by creating virtual serial ports that tunnel data through TCP/IP connections, enabling:

- **Legacy Application Support**: Running unchanged SCADA, HMI, and monitoring software that expects physical COM ports
- **Network Extension**: Overcoming physical cable length limitations of RS-232/RS-485
- **Protocol Bridging**: Converting between Modbus RTU and Modbus TCP seamlessly
- **Remote Access**: Enabling technicians to access field devices from anywhere
- **Infrastructure Modernization**: Gradually migrating serial networks to Ethernet without disrupting operations

The C/C++ implementation provides low-level cross-platform control suitable for embedded systems and resource-constrained environments, while the Rust implementation offers memory safety, async I/O performance, and modern abstractions ideal for reliable, concurrent gateway applications. Both approaches maintain protocol transparency, ensuring legacy applications work without modification while gaining the benefits of network connectivity.