# ICMP Error Messages: Detailed Description and Programming Guide

## Overview

**Internet Control Message Protocol (ICMP)** is a network layer protocol used by network devices to send error messages and operational information. ICMP error messages are critical for diagnosing network problems and implementing proper error handling in network applications.

## ICMP Error Message Types

### 1. **Destination Unreachable (Type 3)**

Sent when a packet cannot be delivered to its destination. Contains various codes:

- **Code 0**: Network Unreachable
- **Code 1**: Host Unreachable
- **Code 2**: Protocol Unreachable
- **Code 3**: Port Unreachable
- **Code 4**: Fragmentation Needed but DF Set
- **Code 5**: Source Route Failed
- **Code 6-15**: Other unreachable conditions

**When Generated:**
- Router can't find route to destination network/host
- Target host/service is not available
- Firewall blocking access
- Required fragmentation but Don't Fragment (DF) flag is set

### 2. **Time Exceeded (Type 11)**

Indicates that a packet was discarded due to timing issues:

- **Code 0**: TTL Exceeded in Transit (hop limit reached)
- **Code 1**: Fragment Reassembly Time Exceeded

**When Generated:**
- Packet's TTL (Time To Live) reaches zero
- Fragments of a datagram don't arrive within reassembly timeout
- Used by `traceroute` utility to map network paths

### 3. **Parameter Problem (Type 12)**

Indicates issues with the IP header:

- **Code 0**: Pointer indicates the error
- **Code 1**: Missing Required Option
- **Code 2**: Bad Length

**When Generated:**
- Malformed IP header
- Invalid header field values
- Missing required options

## ICMP Header Structure

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Type      |     Code      |          Checksum             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             unused                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      Internet Header + 64 bits of Original Data Datagram      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

---

## C/C++ Programming Examples

### Example 1: ICMP Header Structures

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

// ICMP Error Message Types
#define ICMP_DEST_UNREACH    3
#define ICMP_TIME_EXCEEDED   11
#define ICMP_PARAMETERPROB   12

// Destination Unreachable Codes
#define ICMP_NET_UNREACH     0
#define ICMP_HOST_UNREACH    1
#define ICMP_PROT_UNREACH    2
#define ICMP_PORT_UNREACH    3
#define ICMP_FRAG_NEEDED     4

// Time Exceeded Codes
#define ICMP_EXC_TTL         0
#define ICMP_EXC_FRAGTIME    1

// ICMP packet structure
struct icmp_packet {
    struct icmphdr hdr;
    char data[512];
};

// Function to calculate ICMP checksum
uint16_t calculate_checksum(uint16_t *addr, int len) {
    uint32_t sum = 0;
    uint16_t *w = addr;
    int nleft = len;
    uint16_t answer = 0;

    while (nleft > 1) {
        sum += *w++;
        nleft -= 2;
    }

    if (nleft == 1) {
        *(unsigned char *)(&answer) = *(unsigned char *)w;
        sum += answer;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;

    return answer;
}

// Parse and handle ICMP error messages
void handle_icmp_error(struct icmphdr *icmp, struct sockaddr_in *from) {
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &from->sin_addr, addr_str, INET_ADDRSTRLEN);

    printf("ICMP Error from %s:\n", addr_str);

    switch (icmp->type) {
        case ICMP_DEST_UNREACH:
            printf("  Type: Destination Unreachable\n");
            switch (icmp->code) {
                case ICMP_NET_UNREACH:
                    printf("  Code: Network Unreachable\n");
                    break;
                case ICMP_HOST_UNREACH:
                    printf("  Code: Host Unreachable\n");
                    break;
                case ICMP_PROT_UNREACH:
                    printf("  Code: Protocol Unreachable\n");
                    break;
                case ICMP_PORT_UNREACH:
                    printf("  Code: Port Unreachable\n");
                    break;
                case ICMP_FRAG_NEEDED:
                    printf("  Code: Fragmentation Needed but DF Set\n");
                    break;
                default:
                    printf("  Code: %d\n", icmp->code);
            }
            break;

        case ICMP_TIME_EXCEEDED:
            printf("  Type: Time Exceeded\n");
            switch (icmp->code) {
                case ICMP_EXC_TTL:
                    printf("  Code: TTL Exceeded in Transit\n");
                    break;
                case ICMP_EXC_FRAGTIME:
                    printf("  Code: Fragment Reassembly Time Exceeded\n");
                    break;
                default:
                    printf("  Code: %d\n", icmp->code);
            }
            break;

        case ICMP_PARAMETERPROB:
            printf("  Type: Parameter Problem\n");
            printf("  Code: %d\n", icmp->code);
            printf("  Pointer: %d\n", icmp->un.gateway);
            break;

        default:
            printf("  Unknown ICMP type: %d\n", icmp->type);
    }
}
```

### Example 2: ICMP Error Listener

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define BUFFER_SIZE 4096

// Function to receive and process ICMP messages
int icmp_listener() {
    int sock;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    ssize_t bytes_received;

    // Create raw ICMP socket (requires root/CAP_NET_RAW)
    sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        perror("Socket creation failed (need root privileges)");
        return -1;
    }

    printf("Listening for ICMP messages...\n");
    printf("Press Ctrl+C to stop\n\n");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recvfrom(sock, buffer, BUFFER_SIZE, 0,
                                  (struct sockaddr *)&from, &fromlen);

        if (bytes_received < 0) {
            perror("recvfrom failed");
            continue;
        }

        // Parse IP header
        struct ip *ip_hdr = (struct ip *)buffer;
        int ip_hdr_len = ip_hdr->ip_hl * 4;

        // Parse ICMP header
        struct icmphdr *icmp_hdr = (struct icmphdr *)(buffer + ip_hdr_len);

        // Check if it's an error message
        if (icmp_hdr->type == ICMP_DEST_UNREACH ||
            icmp_hdr->type == ICMP_TIME_EXCEEDED ||
            icmp_hdr->type == ICMP_PARAMETERPROB) {
            
            handle_icmp_error(icmp_hdr, &from);
            
            // Extract original IP header from ICMP payload
            struct ip *orig_ip = (struct ip *)(buffer + ip_hdr_len + 8);
            char orig_dest[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &orig_ip->ip_dst, orig_dest, INET_ADDRSTRLEN);
            printf("  Original destination: %s\n", orig_dest);
            printf("  Original protocol: %d\n", orig_ip->ip_p);
            printf("\n");
        }
    }

    close(sock);
    return 0;
}

int main() {
    return icmp_listener();
}
```

### Example 3: Sending UDP with ICMP Error Handling

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define BUFFER_SIZE 1024

int send_udp_with_error_handling(const char *dest_ip, int dest_port) {
    int udp_sock, icmp_sock;
    struct sockaddr_in dest_addr, error_addr;
    char send_buffer[BUFFER_SIZE];
    char icmp_buffer[BUFFER_SIZE];
    socklen_t error_addr_len = sizeof(error_addr);
    fd_set read_fds;
    struct timeval timeout;

    // Create UDP socket
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("UDP socket creation failed");
        return -1;
    }

    // Enable receiving ICMP errors on UDP socket
    int on = 1;
    if (setsockopt(udp_sock, SOL_IP, IP_RECVERR, &on, sizeof(on)) < 0) {
        perror("setsockopt IP_RECVERR failed");
        close(udp_sock);
        return -1;
    }

    // Setup destination
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr);

    // Send UDP packet
    strcpy(send_buffer, "Test UDP packet");
    if (sendto(udp_sock, send_buffer, strlen(send_buffer), 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("sendto failed");
        close(udp_sock);
        return -1;
    }

    printf("UDP packet sent to %s:%d\n", dest_ip, dest_port);
    printf("Waiting for response or ICMP error...\n");

    // Wait for response or error with timeout
    FD_ZERO(&read_fds);
    FD_SET(udp_sock, &read_fds);
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    int ready = select(udp_sock + 1, &read_fds, NULL, NULL, &timeout);

    if (ready > 0) {
        // Check for ICMP errors using MSG_ERRQUEUE
        struct msghdr msg;
        struct iovec iov;
        struct cmsghdr *cmsg;
        char control[BUFFER_SIZE];

        iov.iov_base = icmp_buffer;
        iov.iov_len = sizeof(icmp_buffer);

        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &error_addr;
        msg.msg_namelen = sizeof(error_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        ssize_t err_len = recvmsg(udp_sock, &msg, MSG_ERRQUEUE);
        if (err_len >= 0) {
            printf("Received ICMP error!\n");
            
            // Parse control message for error info
            for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
                 cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVERR) {
                    struct sock_extended_err *err =
                        (struct sock_extended_err *)CMSG_DATA(cmsg);
                    
                    printf("Error origin: %d\n", err->ee_origin);
                    printf("Error type: %d\n", err->ee_type);
                    printf("Error code: %d\n", err->ee_code);
                }
            }
        } else {
            // Normal data received
            ssize_t recv_len = recv(udp_sock, icmp_buffer, BUFFER_SIZE, 0);
            if (recv_len > 0) {
                printf("Received response: %.*s\n", (int)recv_len, icmp_buffer);
            }
        }
    } else if (ready == 0) {
        printf("Timeout - no response or error received\n");
    } else {
        perror("select failed");
    }

    close(udp_sock);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <dest_ip> <dest_port>\n", argv[0]);
        return 1;
    }

    return send_udp_with_error_handling(argv[1], atoi(argv[2]));
}
```

---

## Rust Programming Examples

### Example 1: ICMP Packet Structures and Parsing

```rust
use std::net::Ipv4Addr;
use std::fmt;

// ICMP Message Types
const ICMP_DEST_UNREACH: u8 = 3;
const ICMP_TIME_EXCEEDED: u8 = 11;
const ICMP_PARAMETER_PROBLEM: u8 = 12;

// Destination Unreachable Codes
const ICMP_NET_UNREACH: u8 = 0;
const ICMP_HOST_UNREACH: u8 = 1;
const ICMP_PROT_UNREACH: u8 = 2;
const ICMP_PORT_UNREACH: u8 = 3;
const ICMP_FRAG_NEEDED: u8 = 4;

// Time Exceeded Codes
const ICMP_EXC_TTL: u8 = 0;
const ICMP_EXC_FRAGTIME: u8 = 1;

#[derive(Debug, Clone)]
pub struct IcmpHeader {
    pub icmp_type: u8,
    pub code: u8,
    pub checksum: u16,
    pub rest_of_header: u32,
}

#[derive(Debug)]
pub enum IcmpErrorType {
    DestinationUnreachable(DestUnreachCode),
    TimeExceeded(TimeExceededCode),
    ParameterProblem { pointer: u8 },
    Unknown { icmp_type: u8, code: u8 },
}

#[derive(Debug, Clone, Copy)]
pub enum DestUnreachCode {
    NetworkUnreachable,
    HostUnreachable,
    ProtocolUnreachable,
    PortUnreachable,
    FragmentationNeeded { next_hop_mtu: u16 },
    SourceRouteFailed,
    Other(u8),
}

#[derive(Debug, Clone, Copy)]
pub enum TimeExceededCode {
    TtlExceeded,
    FragmentReassemblyTimeExceeded,
    Other(u8),
}

impl IcmpHeader {
    /// Parse ICMP header from bytes
    pub fn from_bytes(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < 8 {
            return Err("Insufficient data for ICMP header");
        }

        Ok(IcmpHeader {
            icmp_type: data[0],
            code: data[1],
            checksum: u16::from_be_bytes([data[2], data[3]]),
            rest_of_header: u32::from_be_bytes([data[4], data[5], data[6], data[7]]),
        })
    }

    /// Convert to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(8);
        bytes.push(self.icmp_type);
        bytes.push(self.code);
        bytes.extend_from_slice(&self.checksum.to_be_bytes());
        bytes.extend_from_slice(&self.rest_of_header.to_be_bytes());
        bytes
    }

    /// Calculate ICMP checksum
    pub fn calculate_checksum(data: &[u8]) -> u16 {
        let mut sum: u32 = 0;
        let mut i = 0;

        // Sum all 16-bit words
        while i < data.len() - 1 {
            let word = u16::from_be_bytes([data[i], data[i + 1]]);
            sum += word as u32;
            i += 2;
        }

        // Add remaining byte if odd length
        if i < data.len() {
            sum += (data[i] as u32) << 8;
        }

        // Fold 32-bit sum to 16 bits
        while sum >> 16 != 0 {
            sum = (sum & 0xffff) + (sum >> 16);
        }

        !sum as u16
    }

    /// Parse error type from ICMP header
    pub fn parse_error_type(&self) -> IcmpErrorType {
        match self.icmp_type {
            ICMP_DEST_UNREACH => {
                let code = match self.code {
                    ICMP_NET_UNREACH => DestUnreachCode::NetworkUnreachable,
                    ICMP_HOST_UNREACH => DestUnreachCode::HostUnreachable,
                    ICMP_PROT_UNREACH => DestUnreachCode::ProtocolUnreachable,
                    ICMP_PORT_UNREACH => DestUnreachCode::PortUnreachable,
                    ICMP_FRAG_NEEDED => {
                        let mtu = (self.rest_of_header & 0xffff) as u16;
                        DestUnreachCode::FragmentationNeeded { next_hop_mtu: mtu }
                    }
                    5 => DestUnreachCode::SourceRouteFailed,
                    code => DestUnreachCode::Other(code),
                };
                IcmpErrorType::DestinationUnreachable(code)
            }
            ICMP_TIME_EXCEEDED => {
                let code = match self.code {
                    ICMP_EXC_TTL => TimeExceededCode::TtlExceeded,
                    ICMP_EXC_FRAGTIME => TimeExceededCode::FragmentReassemblyTimeExceeded,
                    code => TimeExceededCode::Other(code),
                };
                IcmpErrorType::TimeExceeded(code)
            }
            ICMP_PARAMETER_PROBLEM => {
                let pointer = (self.rest_of_header >> 24) as u8;
                IcmpErrorType::ParameterProblem { pointer }
            }
            _ => IcmpErrorType::Unknown {
                icmp_type: self.icmp_type,
                code: self.code,
            },
        }
    }
}

impl fmt::Display for IcmpErrorType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            IcmpErrorType::DestinationUnreachable(code) => {
                write!(f, "Destination Unreachable: {:?}", code)
            }
            IcmpErrorType::TimeExceeded(code) => {
                write!(f, "Time Exceeded: {:?}", code)
            }
            IcmpErrorType::ParameterProblem { pointer } => {
                write!(f, "Parameter Problem at byte {}", pointer)
            }
            IcmpErrorType::Unknown { icmp_type, code } => {
                write!(f, "Unknown ICMP (Type: {}, Code: {})", icmp_type, code)
            }
        }
    }
}
```

### Example 2: ICMP Error Handler

```rust
use std::net::{IpAddr, Ipv4Addr};
use std::io;

pub struct IcmpErrorHandler {
    source: IpAddr,
    original_dest: IpAddr,
    original_protocol: u8,
}

impl IcmpErrorHandler {
    pub fn new(source: IpAddr, original_dest: IpAddr, original_protocol: u8) -> Self {
        IcmpErrorHandler {
            source,
            original_dest,
            original_protocol,
        }
    }

    /// Handle ICMP error message
    pub fn handle_error(&self, error_type: &IcmpErrorType) {
        println!("ICMP Error from {}", self.source);
        println!("Original destination: {}", self.original_dest);
        println!("Original protocol: {}", self.original_protocol);
        println!("Error: {}", error_type);

        match error_type {
            IcmpErrorType::DestinationUnreachable(code) => {
                self.handle_dest_unreachable(code);
            }
            IcmpErrorType::TimeExceeded(code) => {
                self.handle_time_exceeded(code);
            }
            IcmpErrorType::ParameterProblem { pointer } => {
                self.handle_parameter_problem(*pointer);
            }
            IcmpErrorType::Unknown { icmp_type, code } => {
                println!("Unknown ICMP type {} with code {}", icmp_type, code);
            }
        }
    }

    fn handle_dest_unreachable(&self, code: &DestUnreachCode) {
        match code {
            DestUnreachCode::NetworkUnreachable => {
                println!("Action: Check routing to network");
                println!("Possible cause: No route to destination network");
            }
            DestUnreachCode::HostUnreachable => {
                println!("Action: Verify host is online and reachable");
                println!("Possible cause: Host is down or ARP failed");
            }
            DestUnreachCode::ProtocolUnreachable => {
                println!("Action: Check protocol support on destination");
                println!("Possible cause: Protocol not supported by destination");
            }
            DestUnreachCode::PortUnreachable => {
                println!("Action: Verify service is running on destination port");
                println!("Possible cause: No application listening on port");
            }
            DestUnreachCode::FragmentationNeeded { next_hop_mtu } => {
                println!("Action: Reduce packet size or clear DF flag");
                println!("Next hop MTU: {} bytes", next_hop_mtu);
                println!("Possible cause: Path MTU discovery issue");
            }
            DestUnreachCode::SourceRouteFailed => {
                println!("Action: Remove source routing or check route");
                println!("Possible cause: Source route cannot be followed");
            }
            DestUnreachCode::Other(code) => {
                println!("Other destination unreachable code: {}", code);
            }
        }
    }

    fn handle_time_exceeded(&self, code: &TimeExceededCode) {
        match code {
            TimeExceededCode::TtlExceeded => {
                println!("Action: Increase TTL or check for routing loops");
                println!("Possible cause: TTL expired, too many hops, or routing loop");
            }
            TimeExceededCode::FragmentReassemblyTimeExceeded => {
                println!("Action: Reduce fragmentation or check network reliability");
                println!("Possible cause: Fragments arriving too slowly");
            }
            TimeExceededCode::Other(code) => {
                println!("Other time exceeded code: {}", code);
            }
        }
    }

    fn handle_parameter_problem(&self, pointer: u8) {
        println!("Action: Check IP header format");
        println!("Error at byte offset: {}", pointer);
        println!("Possible cause: Malformed IP header or invalid option");
    }

    /// Determine if error is recoverable
    pub fn is_recoverable(&self, error_type: &IcmpErrorType) -> bool {
        match error_type {
            IcmpErrorType::DestinationUnreachable(code) => match code {
                DestUnreachCode::NetworkUnreachable => false,
                DestUnreachCode::HostUnreachable => false,
                DestUnreachCode::PortUnreachable => false,
                DestUnreachCode::FragmentationNeeded { .. } => true, // Can retry with smaller MTU
                _ => false,
            },
            IcmpErrorType::TimeExceeded(code) => match code {
                TimeExceededCode::TtlExceeded => true, // Can retry with higher TTL
                _ => false,
            },
            _ => false,
        }
    }
}
```

### Example 3: Async ICMP Socket with Tokio

```rust
use tokio::net::UdpSocket;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::io;
use tokio::time::{timeout, Duration};

pub struct IcmpAwareSocket {
    socket: UdpSocket,
}

impl IcmpAwareSocket {
    /// Create a new UDP socket with ICMP error reporting
    pub async fn new(bind_addr: &str) -> io::Result<Self> {
        let socket = UdpSocket::bind(bind_addr).await?;
        
        // Enable ICMP error messages (platform-specific)
        #[cfg(target_os = "linux")]
        {
            use std::os::unix::io::AsRawFd;
            unsafe {
                let fd = socket.as_raw_fd();
                let enable: libc::c_int = 1;
                libc::setsockopt(
                    fd,
                    libc::SOL_IP,
                    libc::IP_RECVERR,
                    &enable as *const _ as *const libc::c_void,
                    std::mem::size_of::<libc::c_int>() as libc::socklen_t,
                );
            }
        }

        Ok(IcmpAwareSocket { socket })
    }

    /// Send data and wait for response or ICMP error
    pub async fn send_with_error_handling(
        &self,
        data: &[u8],
        dest: SocketAddr,
        timeout_secs: u64,
    ) -> Result<Vec<u8>, IcmpError> {
        // Send packet
        self.socket.send_to(data, dest).await
            .map_err(|e| IcmpError::SendFailed(e.to_string()))?;

        println!("Sent {} bytes to {}", data.len(), dest);

        // Wait for response with timeout
        let mut buffer = vec![0u8; 4096];
        
        match timeout(Duration::from_secs(timeout_secs), self.socket.recv_from(&mut buffer)).await {
            Ok(Ok((len, from))) => {
                println!("Received {} bytes from {}", len, from);
                buffer.truncate(len);
                Ok(buffer)
            }
            Ok(Err(e)) => {
                // Check if it's an ICMP error
                if e.kind() == io::ErrorKind::ConnectionRefused {
                    Err(IcmpError::PortUnreachable)
                } else if e.raw_os_error() == Some(libc::EHOSTUNREACH) {
                    Err(IcmpError::HostUnreachable)
                } else if e.raw_os_error() == Some(libc::ENETUNREACH) {
                    Err(IcmpError::NetworkUnreachable)
                } else {
                    Err(IcmpError::RecvFailed(e.to_string()))
                }
            }
            Err(_) => Err(IcmpError::Timeout),
        }
    }

    /// Get local address
    pub fn local_addr(&self) -> io::Result<SocketAddr> {
        self.socket.local_addr()
    }
}

#[derive(Debug)]
pub enum IcmpError {
    NetworkUnreachable,
    HostUnreachable,
    PortUnreachable,
    Timeout,
    SendFailed(String),
    RecvFailed(String),
}

impl std::fmt::Display for IcmpError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            IcmpError::NetworkUnreachable => write!(f, "Network unreachable"),
            IcmpError::HostUnreachable => write!(f, "Host unreachable"),
            IcmpError::PortUnreachable => write!(f, "Port unreachable"),
            IcmpError::Timeout => write!(f, "Request timed out"),
            IcmpError::SendFailed(msg) => write!(f, "Send failed: {}", msg),
            IcmpError::RecvFailed(msg) => write!(f, "Receive failed: {}", msg),
        }
    }
}

impl std::error::Error for IcmpError {}

// Example usage
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let socket = IcmpAwareSocket::new("0.0.0.0:0").await?;
    println!("Socket bound to {}", socket.local_addr()?);

    let dest = "8.8.8.8:53".parse()?;
    let data = b"DNS query simulation";

    match socket.send_with_error_handling(data, dest, 5).await {
        Ok(response) => {
            println!("Success! Received response: {} bytes", response.len());
        }
        Err(e) => {
            println!("ICMP Error occurred: {}", e);
            match e {
                IcmpError::PortUnreachable => {
                    println!("Service not available on destination port");
                }
                IcmpError::HostUnreachable => {
                    println!("Cannot reach destination host");
                }
                IcmpError::Timeout => {
                    println!("No response received within timeout period");
                }
                _ => {}
            }
        }
    }

    Ok(())
}
```

---

## Summary

**ICMP Error Messages** are essential for network diagnostics and proper error handling in network applications:

### Key Points:

1. **Three Main Error Types:**
   - **Destination Unreachable (Type 3)**: Network/host/port unavailable, fragmentation issues
   - **Time Exceeded (Type 11)**: TTL expired or fragment timeout (used by traceroute)
   - **Parameter Problem (Type 12)**: Malformed IP headers

2. **Programming Considerations:**
   - **C/C++**: Use raw sockets (requires privileges), handle `IP_RECVERR` for error queue
   - **Rust**: Leverage type safety for error handling, use async/await for modern network code
   - Both languages need proper checksum calculation and packet parsing

3. **Practical Applications:**
   - Implement Path MTU Discovery (PMTUD)
   - Build traceroute-like utilities
   - Proper error handling in UDP applications
   - Network monitoring and diagnostics tools

4. **Best Practices:**
   - Always validate packet structure before parsing
   - Implement timeouts for network operations
   - Log ICMP errors for debugging
   - Handle different error codes appropriately
   - Consider error recoverability (retry logic)

5. **Security Note:**
   - ICMP can be used for reconnaissance
   - Rate-limit ICMP error generation
   - Validate source addresses
   - Be cautious with ICMP redirect messages

ICMP error messages provide critical feedback about network conditions, enabling applications to make intelligent decisions about retries, path selection, and error reporting to users.