# TCP Keep-Alive Mechanism in Modbus

## Overview

The TCP Keep-Alive mechanism is a critical feature for maintaining reliable Modbus TCP connections, especially in industrial environments where network interruptions, firewall timeouts, or silent connection failures can occur. This mechanism implements heartbeat functionality and connection health monitoring to detect and recover from "half-open" connections—situations where one end of a TCP connection has failed without properly closing the connection.

## Why Keep-Alive Matters in Modbus TCP

In Modbus TCP communications, several scenarios can lead to connection problems:

1. **Silent Network Failures**: A cable gets disconnected, but neither endpoint receives a TCP reset
2. **Firewall Timeouts**: Stateful firewalls drop idle connections after a timeout period
3. **Device Crashes**: A PLC or slave device reboots without sending proper connection termination
4. **Long Idle Periods**: Industrial processes may have long periods without data exchange

Without keep-alive mechanisms, an application might wait indefinitely for a response on a dead connection, leading to application hangs and delayed fault detection.

## Implementation Approaches

There are two primary approaches to implementing keep-alive in Modbus TCP:

### 1. TCP Socket-Level Keep-Alive
Using the operating system's built-in TCP keep-alive feature (SO_KEEPALIVE socket option).

### 2. Application-Level Heartbeat
Sending periodic Modbus diagnostic messages or dummy requests to verify connection health.

## Detailed C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

// Modbus TCP header structure
typedef struct {
    uint16_t transaction_id;
    uint16_t protocol_id;      // Always 0 for Modbus
    uint16_t length;
    uint8_t unit_id;
} modbus_tcp_header_t;

// Keep-alive configuration
typedef struct {
    int keepalive_time;        // Time before first probe (seconds)
    int keepalive_interval;    // Interval between probes (seconds)
    int keepalive_probes;      // Number of probes before timeout
} keepalive_config_t;

/**
 * Enable TCP keep-alive at the socket level
 * This is the OS-level mechanism
 */
int enable_tcp_keepalive(int sockfd, keepalive_config_t *config) {
    int optval = 1;
    
    // Enable keep-alive
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_KEEPALIVE");
        return -1;
    }
    
#ifdef __linux__
    // Set time before first keep-alive probe
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, 
                   &config->keepalive_time, sizeof(config->keepalive_time)) < 0) {
        perror("setsockopt TCP_KEEPIDLE");
        return -1;
    }
    
    // Set interval between keep-alive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, 
                   &config->keepalive_interval, sizeof(config->keepalive_interval)) < 0) {
        perror("setsockopt TCP_KEEPINTVL");
        return -1;
    }
    
    // Set number of keep-alive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, 
                   &config->keepalive_probes, sizeof(config->keepalive_probes)) < 0) {
        perror("setsockopt TCP_KEEPCNT");
        return -1;
    }
#elif defined(__APPLE__)
    // macOS uses TCP_KEEPALIVE instead of TCP_KEEPIDLE
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPALIVE, 
                   &config->keepalive_time, sizeof(config->keepalive_time)) < 0) {
        perror("setsockopt TCP_KEEPALIVE");
        return -1;
    }
#endif
    
    printf("TCP Keep-Alive enabled: time=%ds, interval=%ds, probes=%d\n",
           config->keepalive_time, config->keepalive_interval, config->keepalive_probes);
    
    return 0;
}

/**
 * Send Modbus diagnostic message (function code 0x08)
 * Used for application-level heartbeat
 */
int send_modbus_heartbeat(int sockfd, uint16_t transaction_id) {
    uint8_t request[12];
    modbus_tcp_header_t *header = (modbus_tcp_header_t *)request;
    
    // Build MBAP header
    header->transaction_id = htons(transaction_id);
    header->protocol_id = 0;
    header->length = htons(6);  // 6 bytes following
    header->unit_id = 1;
    
    // Diagnostic function (0x08), subfunction 0x0000 (Return Query Data)
    request[7] = 0x08;  // Function code
    request[8] = 0x00;  // Sub-function high byte
    request[9] = 0x00;  // Sub-function low byte
    request[10] = 0xAA; // Test data high byte
    request[11] = 0x55; // Test data low byte
    
    if (send(sockfd, request, sizeof(request), 0) < 0) {
        perror("send heartbeat");
        return -1;
    }
    
    return 0;
}

/**
 * Receive and validate heartbeat response
 */
int receive_modbus_heartbeat_response(int sockfd, uint16_t expected_transaction_id) {
    uint8_t response[12];
    ssize_t n;
    
    // Set receive timeout
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    n = recv(sockfd, response, sizeof(response), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "Heartbeat timeout - connection may be dead\n");
        } else {
            perror("recv heartbeat response");
        }
        return -1;
    }
    
    if (n < 12) {
        fprintf(stderr, "Incomplete heartbeat response\n");
        return -1;
    }
    
    // Validate response
    modbus_tcp_header_t *header = (modbus_tcp_header_t *)response;
    if (ntohs(header->transaction_id) != expected_transaction_id) {
        fprintf(stderr, "Transaction ID mismatch in heartbeat\n");
        return -1;
    }
    
    if (response[7] != 0x08) {
        fprintf(stderr, "Invalid function code in heartbeat response\n");
        return -1;
    }
    
    return 0;
}

/**
 * Connection health monitor with both mechanisms
 */
typedef struct {
    int sockfd;
    time_t last_activity;
    int heartbeat_interval;    // Application-level heartbeat interval (seconds)
    uint16_t transaction_id;
    int consecutive_failures;
    int max_failures;
} connection_monitor_t;

void init_connection_monitor(connection_monitor_t *monitor, int sockfd, int heartbeat_interval) {
    monitor->sockfd = sockfd;
    monitor->last_activity = time(NULL);
    monitor->heartbeat_interval = heartbeat_interval;
    monitor->transaction_id = 1;
    monitor->consecutive_failures = 0;
    monitor->max_failures = 3;
}

int check_connection_health(connection_monitor_t *monitor) {
    time_t now = time(NULL);
    time_t idle_time = now - monitor->last_activity;
    
    // Check if we need to send a heartbeat
    if (idle_time >= monitor->heartbeat_interval) {
        printf("Sending application-level heartbeat (idle for %ld seconds)\n", idle_time);
        
        if (send_modbus_heartbeat(monitor->sockfd, monitor->transaction_id) < 0) {
            monitor->consecutive_failures++;
            fprintf(stderr, "Heartbeat send failed (%d/%d)\n", 
                    monitor->consecutive_failures, monitor->max_failures);
            return -1;
        }
        
        if (receive_modbus_heartbeat_response(monitor->sockfd, monitor->transaction_id) < 0) {
            monitor->consecutive_failures++;
            fprintf(stderr, "Heartbeat response failed (%d/%d)\n", 
                    monitor->consecutive_failures, monitor->max_failures);
            
            if (monitor->consecutive_failures >= monitor->max_failures) {
                fprintf(stderr, "Connection declared dead after %d failures\n", 
                        monitor->max_failures);
                return -1;
            }
        } else {
            // Success - reset failure counter
            monitor->consecutive_failures = 0;
            monitor->last_activity = now;
            printf("Heartbeat successful - connection healthy\n");
        }
        
        monitor->transaction_id++;
    }
    
    return 0;
}

// Example usage
int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    keepalive_config_t ka_config = {
        .keepalive_time = 30,      // Start probing after 30 seconds
        .keepalive_interval = 5,   // Probe every 5 seconds
        .keepalive_probes = 3      // 3 probes before declaring dead
    };
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }
    
    // Enable TCP keep-alive
    if (enable_tcp_keepalive(sockfd, &ka_config) < 0) {
        close(sockfd);
        return 1;
    }
    
    // Connect to Modbus server
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(502);
    inet_pton(AF_INET, "192.168.1.100", &server_addr.sin_addr);
    
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }
    
    printf("Connected to Modbus server\n");
    
    // Initialize connection monitor
    connection_monitor_t monitor;
    init_connection_monitor(&monitor, sockfd, 15);  // 15-second heartbeat
    
    // Main loop with periodic health checks
    while (1) {
        if (check_connection_health(&monitor) < 0) {
            fprintf(stderr, "Connection unhealthy - attempting reconnect\n");
            close(sockfd);
            // Reconnection logic would go here
            break;
        }
        
        // Simulate other work
        sleep(5);
    }
    
    close(sockfd);
    return 0;
}
```

## Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::net::{TcpStream, SocketAddr};
use std::time::{Duration, Instant};
use std::thread;

// Modbus TCP header
#[repr(C, packed)]
struct ModbusTcpHeader {
    transaction_id: u16,
    protocol_id: u16,
    length: u16,
    unit_id: u8,
}

// Keep-alive configuration
#[derive(Clone, Copy)]
struct KeepAliveConfig {
    keepalive_time: u32,      // Seconds before first probe
    keepalive_interval: u32,  // Seconds between probes
    keepalive_probes: u32,    // Number of probes
}

impl KeepAliveConfig {
    fn new(time: u32, interval: u32, probes: u32) -> Self {
        Self {
            keepalive_time: time,
            keepalive_interval: interval,
            keepalive_probes: probes,
        }
    }
}

// Connection health monitor
struct ConnectionMonitor {
    stream: TcpStream,
    last_activity: Instant,
    heartbeat_interval: Duration,
    transaction_id: u16,
    consecutive_failures: u32,
    max_failures: u32,
}

impl ConnectionMonitor {
    fn new(stream: TcpStream, heartbeat_interval: Duration) -> Self {
        Self {
            stream,
            last_activity: Instant::now(),
            heartbeat_interval,
            transaction_id: 1,
            consecutive_failures: 0,
            max_failures: 3,
        }
    }

    /// Enable TCP keep-alive on the socket
    fn enable_tcp_keepalive(&self, config: KeepAliveConfig) -> io::Result<()> {
        use socket2::{Socket, TcpKeepalive};
        
        let socket = Socket::from(self.stream.try_clone()?);
        
        // Create keep-alive configuration
        let keepalive = TcpKeepalive::new()
            .with_time(Duration::from_secs(config.keepalive_time as u64))
            .with_interval(Duration::from_secs(config.keepalive_interval as u64));
        
        // On Linux, we can also set the probe count
        #[cfg(target_os = "linux")]
        let keepalive = keepalive.with_retries(config.keepalive_probes);
        
        socket.set_tcp_keepalive(&keepalive)?;
        
        println!(
            "TCP Keep-Alive enabled: time={}s, interval={}s, probes={}",
            config.keepalive_time, config.keepalive_interval, config.keepalive_probes
        );
        
        Ok(())
    }

    /// Send Modbus diagnostic heartbeat (Function 0x08)
    fn send_heartbeat(&mut self) -> io::Result<()> {
        let mut request = vec![0u8; 12];
        
        // Build MBAP header
        request[0..2].copy_from_slice(&self.transaction_id.to_be_bytes());
        request[2..4].copy_from_slice(&0u16.to_be_bytes()); // Protocol ID
        request[4..6].copy_from_slice(&6u16.to_be_bytes()); // Length
        request[6] = 1; // Unit ID
        
        // Diagnostic function (0x08), subfunction 0x0000
        request[7] = 0x08;  // Function code
        request[8] = 0x00;  // Sub-function high
        request[9] = 0x00;  // Sub-function low
        request[10] = 0xAA; // Test data high
        request[11] = 0x55; // Test data low
        
        self.stream.write_all(&request)?;
        Ok(())
    }

    /// Receive and validate heartbeat response
    fn receive_heartbeat_response(&mut self) -> io::Result<()> {
        let mut response = vec![0u8; 12];
        
        // Set read timeout
        self.stream.set_read_timeout(Some(Duration::from_secs(3)))?;
        
        self.stream.read_exact(&mut response)?;
        
        // Validate response
        let recv_transaction_id = u16::from_be_bytes([response[0], response[1]]);
        if recv_transaction_id != self.transaction_id {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Transaction ID mismatch in heartbeat",
            ));
        }
        
        if response[7] != 0x08 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Invalid function code in heartbeat response",
            ));
        }
        
        Ok(())
    }

    /// Check connection health and send heartbeat if needed
    fn check_health(&mut self) -> io::Result<bool> {
        let idle_time = self.last_activity.elapsed();
        
        if idle_time >= self.heartbeat_interval {
            println!("Sending application-level heartbeat (idle for {:?})", idle_time);
            
            match self.send_heartbeat() {
                Ok(_) => {
                    match self.receive_heartbeat_response() {
                        Ok(_) => {
                            self.consecutive_failures = 0;
                            self.last_activity = Instant::now();
                            println!("Heartbeat successful - connection healthy");
                            self.transaction_id = self.transaction_id.wrapping_add(1);
                        }
                        Err(e) => {
                            self.consecutive_failures += 1;
                            eprintln!(
                                "Heartbeat response failed ({}/{}): {}",
                                self.consecutive_failures, self.max_failures, e
                            );
                            
                            if self.consecutive_failures >= self.max_failures {
                                eprintln!(
                                    "Connection declared dead after {} failures",
                                    self.max_failures
                                );
                                return Ok(false);
                            }
                        }
                    }
                }
                Err(e) => {
                    self.consecutive_failures += 1;
                    eprintln!(
                        "Heartbeat send failed ({}/{}): {}",
                        self.consecutive_failures, self.max_failures, e
                    );
                    
                    if self.consecutive_failures >= self.max_failures {
                        return Ok(false);
                    }
                }
            }
        }
        
        Ok(true)
    }

    /// Update activity timestamp (call after successful communication)
    fn update_activity(&mut self) {
        self.last_activity = Instant::now();
    }
}

// Example usage
fn main() -> io::Result<()> {
    let addr: SocketAddr = "192.168.1.100:502".parse()
        .expect("Invalid address");
    
    let stream = TcpStream::connect(addr)?;
    println!("Connected to Modbus server at {}", addr);
    
    let mut monitor = ConnectionMonitor::new(
        stream,
        Duration::from_secs(15), // 15-second heartbeat interval
    );
    
    // Enable TCP keep-alive
    let ka_config = KeepAliveConfig::new(30, 5, 3);
    monitor.enable_tcp_keepalive(ka_config)?;
    
    // Main monitoring loop
    loop {
        match monitor.check_health() {
            Ok(true) => {
                // Connection is healthy
                // Perform normal Modbus operations here
            }
            Ok(false) => {
                eprintln!("Connection unhealthy - reconnection needed");
                break;
            }
            Err(e) => {
                eprintln!("Health check error: {}", e);
                break;
            }
        }
        
        // Simulate other work
        thread::sleep(Duration::from_secs(5));
    }
    
    Ok(())
}
```

## Summary

The TCP Keep-Alive mechanism is essential for robust Modbus TCP implementations, providing two complementary layers of connection health monitoring:

**TCP Socket-Level Keep-Alive**: The operating system sends periodic TCP probes to detect dead connections at the transport layer. This is configured via socket options (SO_KEEPALIVE, TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT) and operates transparently without application awareness.

**Application-Level Heartbeat**: The application sends Modbus diagnostic messages (function code 0x08) at regular intervals during idle periods. This provides faster detection of connection issues and validates the entire communication stack, including the Modbus application layer.

**Key Benefits**: Early detection of half-open connections, prevention of indefinite hangs on dead connections, firewall timeout prevention, and improved fault tolerance in industrial environments.

**Best Practices**: Use both mechanisms together—TCP keep-alive for low-level network failures (30-60 second intervals) and application heartbeats for faster application-layer detection (10-20 second intervals). Implement exponential backoff for reconnection attempts, log connection health events for diagnostics, and tune parameters based on your network environment and acceptable detection latency.