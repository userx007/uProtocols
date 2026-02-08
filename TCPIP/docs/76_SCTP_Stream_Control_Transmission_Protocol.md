# SCTP (Stream Control Transmission Protocol)

## Overview

SCTP is a transport layer protocol (like TCP and UDP) that combines the best features of both while adding unique capabilities. Defined in RFC 4960, SCTP provides reliable, message-oriented communication with advanced features like multi-streaming and multi-homing. It was originally designed for transporting telephony signaling over IP networks but has applications in any scenario requiring reliable, ordered delivery with enhanced features.

## Key Features

### 1. **Multi-streaming**
Unlike TCP's single byte stream, SCTP supports multiple independent streams within a single association. This prevents head-of-line blocking—if one stream experiences packet loss, other streams can continue delivering data.

### 2. **Multi-homing**
SCTP supports multiple IP addresses per endpoint, providing automatic failover and increased reliability. If one network path fails, communication continues through alternate paths.

### 3. **Message-oriented**
SCTP preserves message boundaries (like UDP) while providing reliability (like TCP). Applications receive complete messages, not byte streams.

### 4. **Four-way Handshake**
SCTP uses a four-way handshake with cookies to prevent SYN flood attacks, making it more resistant to DoS attacks than TCP's three-way handshake.

### 5. **Built-in Heartbeats**
Automatic path monitoring detects network failures without application-level keepalives.

## SCTP vs TCP vs UDP

| Feature | TCP | UDP | SCTP |
|---------|-----|-----|------|
| Reliability | Yes | No | Yes |
| Ordering | Strict | No | Per-stream |
| Message boundaries | No | Yes | Yes |
| Multi-streaming | No | No | Yes |
| Multi-homing | No | No | Yes |
| Head-of-line blocking | Yes | N/A | No |

## Programming Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// SCTP Server Example
int sctp_server_example() {
    int listen_fd, conn_fd;
    struct sockaddr_in servaddr;
    struct sctp_initmsg initmsg;
    struct sctp_event_subscribe events;
    char buffer[1024];
    struct sctp_sndrcvinfo sndrcvinfo;
    int flags;
    
    // Create SCTP socket
    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // Configure multiple streams
    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams = 5;  // 5 outbound streams
    initmsg.sinit_max_instreams = 5; // 5 inbound streams
    initmsg.sinit_max_attempts = 4;
    
    if (setsockopt(listen_fd, IPPROTO_SCTP, SCTP_INITMSG, 
                   &initmsg, sizeof(initmsg)) < 0) {
        perror("setsockopt SCTP_INITMSG");
        close(listen_fd);
        return -1;
    }
    
    // Enable events for notifications
    memset(&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;
    events.sctp_association_event = 1;
    events.sctp_send_failure_event = 1;
    
    if (setsockopt(listen_fd, IPPROTO_SCTP, SCTP_EVENTS,
                   &events, sizeof(events)) < 0) {
        perror("setsockopt SCTP_EVENTS");
        close(listen_fd);
        return -1;
    }
    
    // Bind to address
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(9999);
    
    if (bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }
    
    // Listen for connections
    if (listen(listen_fd, 5) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }
    
    printf("SCTP Server listening on port 9999...\n");
    
    // Accept connection
    conn_fd = accept(listen_fd, NULL, NULL);
    if (conn_fd < 0) {
        perror("accept");
        close(listen_fd);
        return -1;
    }
    
    printf("Client connected\n");
    
    // Receive message with stream information
    ssize_t n = sctp_recvmsg(conn_fd, buffer, sizeof(buffer),
                             NULL, 0, &sndrcvinfo, &flags);
    
    if (n > 0) {
        buffer[n] = '\0';
        printf("Received on stream %d: %s\n", 
               sndrcvinfo.sinfo_stream, buffer);
        
        // Send response on same stream
        const char *response = "Message received";
        sctp_sendmsg(conn_fd, response, strlen(response),
                     NULL, 0, 0, 0,
                     sndrcvinfo.sinfo_stream, // Use same stream
                     0, 0);
    }
    
    close(conn_fd);
    close(listen_fd);
    return 0;
}

// SCTP Client Example
int sctp_client_example() {
    int sock_fd;
    struct sockaddr_in servaddr;
    struct sctp_initmsg initmsg;
    const char *messages[] = {
        "Stream 0 message",
        "Stream 1 message",
        "Stream 2 message"
    };
    
    // Create SCTP socket
    sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // Configure streams
    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams = 5;
    initmsg.sinit_max_instreams = 5;
    
    setsockopt(sock_fd, IPPROTO_SCTP, SCTP_INITMSG,
               &initmsg, sizeof(initmsg));
    
    // Connect to server
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9999);
    inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
    
    if (connect(sock_fd, (struct sockaddr *)&servaddr, 
                sizeof(servaddr)) < 0) {
        perror("connect");
        close(sock_fd);
        return -1;
    }
    
    printf("Connected to SCTP server\n");
    
    // Send messages on different streams
    for (int i = 0; i < 3; i++) {
        sctp_sendmsg(sock_fd, messages[i], strlen(messages[i]),
                     NULL, 0,
                     0,    // ppid
                     0,    // flags
                     i,    // stream number
                     0,    // timetolive
                     0);   // context
        printf("Sent on stream %d: %s\n", i, messages[i]);
    }
    
    close(sock_fd);
    return 0;
}

// Multi-homing Example (binding multiple addresses)
int sctp_multihoming_server() {
    int sock_fd;
    struct sockaddr_in addr1, addr2;
    
    sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // Bind first address
    memset(&addr1, 0, sizeof(addr1));
    addr1.sin_family = AF_INET;
    addr1.sin_port = htons(9999);
    inet_pton(AF_INET, "192.168.1.10", &addr1.sin_addr);
    
    if (bind(sock_fd, (struct sockaddr *)&addr1, sizeof(addr1)) < 0) {
        perror("bind addr1");
        close(sock_fd);
        return -1;
    }
    
    // Bind additional address using sctp_bindx
    memset(&addr2, 0, sizeof(addr2));
    addr2.sin_family = AF_INET;
    addr2.sin_port = htons(9999);
    inet_pton(AF_INET, "192.168.2.10", &addr2.sin_addr);
    
    if (sctp_bindx(sock_fd, (struct sockaddr *)&addr2, 1, 
                   SCTP_BINDX_ADD_ADDR) < 0) {
        perror("sctp_bindx");
        close(sock_fd);
        return -1;
    }
    
    printf("Bound to multiple addresses for multi-homing\n");
    
    listen(sock_fd, 5);
    // ... accept and handle connections ...
    
    close(sock_fd);
    return 0;
}
```

### Rust Implementation

```rust
// Note: SCTP support in Rust is limited. This uses FFI to system calls.
// For production, consider using crates like 'usrsctp' or system bindings.

use std::io::{self, Error, ErrorKind};
use std::net::{SocketAddr, IpAddr, Ipv4Addr};
use std::mem;
use std::ptr;

// FFI declarations for SCTP
#[cfg(target_os = "linux")]
mod sctp_ffi {
    use libc::{c_int, c_void, size_t, sockaddr, socklen_t};
    
    pub const IPPROTO_SCTP: c_int = 132;
    pub const SCTP_INITMSG: c_int = 2;
    pub const SCTP_EVENTS: c_int = 11;
    pub const SOCK_STREAM: c_int = 1;
    
    #[repr(C)]
    pub struct sctp_initmsg {
        pub sinit_num_ostreams: u16,
        pub sinit_max_instreams: u16,
        pub sinit_max_attempts: u16,
        pub sinit_max_init_timeo: u16,
    }
    
    #[repr(C)]
    pub struct sctp_sndrcvinfo {
        pub sinfo_stream: u16,
        pub sinfo_ssn: u16,
        pub sinfo_flags: u16,
        pub sinfo_ppid: u32,
        pub sinfo_context: u32,
        pub sinfo_timetolive: u32,
        pub sinfo_tsn: u32,
        pub sinfo_cumtsn: u32,
        pub sinfo_assoc_id: i32,
    }
    
    extern "C" {
        pub fn sctp_sendmsg(
            s: c_int,
            msg: *const c_void,
            len: size_t,
            to: *const sockaddr,
            tolen: socklen_t,
            ppid: u32,
            flags: u32,
            stream_no: u16,
            timetolive: u32,
            context: u32,
        ) -> isize;
        
        pub fn sctp_recvmsg(
            s: c_int,
            msg: *mut c_void,
            len: size_t,
            from: *mut sockaddr,
            fromlen: *mut socklen_t,
            sinfo: *mut sctp_sndrcvinfo,
            msg_flags: *mut c_int,
        ) -> isize;
    }
}

#[cfg(target_os = "linux")]
pub struct SctpSocket {
    fd: i32,
}

#[cfg(target_os = "linux")]
impl SctpSocket {
    pub fn new() -> io::Result<Self> {
        use libc::{socket, AF_INET, SOCK_STREAM};
        
        let fd = unsafe {
            socket(AF_INET, SOCK_STREAM, sctp_ffi::IPPROTO_SCTP)
        };
        
        if fd < 0 {
            return Err(Error::last_os_error());
        }
        
        Ok(SctpSocket { fd })
    }
    
    pub fn set_init_options(&self, out_streams: u16, in_streams: u16) -> io::Result<()> {
        use libc::{setsockopt, SOL_SOCKET};
        
        let init_msg = sctp_ffi::sctp_initmsg {
            sinit_num_ostreams: out_streams,
            sinit_max_instreams: in_streams,
            sinit_max_attempts: 4,
            sinit_max_init_timeo: 0,
        };
        
        let ret = unsafe {
            setsockopt(
                self.fd,
                sctp_ffi::IPPROTO_SCTP,
                sctp_ffi::SCTP_INITMSG,
                &init_msg as *const _ as *const libc::c_void,
                mem::size_of::<sctp_ffi::sctp_initmsg>() as u32,
            )
        };
        
        if ret < 0 {
            return Err(Error::last_os_error());
        }
        
        Ok(())
    }
    
    pub fn bind(&self, addr: SocketAddr) -> io::Result<()> {
        use libc::{bind, sockaddr_in, AF_INET};
        use std::mem;
        
        let sin = sockaddr_in {
            sin_family: AF_INET as u16,
            sin_port: addr.port().to_be(),
            sin_addr: libc::in_addr {
                s_addr: match addr.ip() {
                    IpAddr::V4(ip) => u32::from_ne_bytes(ip.octets()),
                    _ => return Err(Error::new(ErrorKind::InvalidInput, "IPv6 not supported")),
                },
            },
            sin_zero: [0; 8],
        };
        
        let ret = unsafe {
            bind(
                self.fd,
                &sin as *const _ as *const libc::sockaddr,
                mem::size_of::<sockaddr_in>() as u32,
            )
        };
        
        if ret < 0 {
            return Err(Error::last_os_error());
        }
        
        Ok(())
    }
    
    pub fn listen(&self, backlog: i32) -> io::Result<()> {
        let ret = unsafe { libc::listen(self.fd, backlog) };
        
        if ret < 0 {
            return Err(Error::last_os_error());
        }
        
        Ok(())
    }
    
    pub fn send_on_stream(&self, data: &[u8], stream: u16) -> io::Result<usize> {
        let ret = unsafe {
            sctp_ffi::sctp_sendmsg(
                self.fd,
                data.as_ptr() as *const libc::c_void,
                data.len(),
                ptr::null(),
                0,
                0,  // ppid
                0,  // flags
                stream,
                0,  // timetolive
                0,  // context
            )
        };
        
        if ret < 0 {
            return Err(Error::last_os_error());
        }
        
        Ok(ret as usize)
    }
    
    pub fn recv_with_info(&self, buf: &mut [u8]) -> io::Result<(usize, u16)> {
        let mut sinfo = unsafe { mem::zeroed::<sctp_ffi::sctp_sndrcvinfo>() };
        let mut flags: i32 = 0;
        
        let ret = unsafe {
            sctp_ffi::sctp_recvmsg(
                self.fd,
                buf.as_mut_ptr() as *mut libc::c_void,
                buf.len(),
                ptr::null_mut(),
                ptr::null_mut(),
                &mut sinfo,
                &mut flags,
            )
        };
        
        if ret < 0 {
            return Err(Error::last_os_error());
        }
        
        Ok((ret as usize, sinfo.sinfo_stream))
    }
}

#[cfg(target_os = "linux")]
impl Drop for SctpSocket {
    fn drop(&mut self) {
        unsafe { libc::close(self.fd) };
    }
}

// Example usage
#[cfg(target_os = "linux")]
fn sctp_server_example() -> io::Result<()> {
    let socket = SctpSocket::new()?;
    socket.set_init_options(5, 5)?;
    
    let addr = SocketAddr::new(
        IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0)),
        9999
    );
    socket.bind(addr)?;
    socket.listen(5)?;
    
    println!("SCTP Server listening on port 9999...");
    
    let mut buffer = vec![0u8; 1024];
    let (n, stream) = socket.recv_with_info(&mut buffer)?;
    
    let message = String::from_utf8_lossy(&buffer[..n]);
    println!("Received on stream {}: {}", stream, message);
    
    socket.send_on_stream(b"Response", stream)?;
    
    Ok(())
}

#[cfg(target_os = "linux")]
fn sctp_client_example() -> io::Result<()> {
    let socket = SctpSocket::new()?;
    socket.set_init_options(5, 5)?;
    
    // Connect logic would go here
    
    // Send on different streams
    socket.send_on_stream(b"Stream 0 data", 0)?;
    socket.send_on_stream(b"Stream 1 data", 1)?;
    socket.send_on_stream(b"Stream 2 data", 2)?;
    
    println!("Sent messages on multiple streams");
    
    Ok(())
}
```

## Use Cases

1. **Telecommunications**: SS7 signaling transport (SIGTRAN)
2. **WebRTC**: Data channel protocol (underlying transport)
3. **Diameter**: AAA protocol in mobile networks
4. **High-availability systems**: Leveraging multi-homing for redundancy
5. **Multi-media streaming**: Using different streams for audio/video/data

## Advantages

- **No head-of-line blocking**: Independent streams prevent one slow stream from blocking others
- **Built-in redundancy**: Multi-homing provides automatic failover
- **DoS resistance**: Cookie-based handshake prevents SYN floods
- **Message boundaries**: Simplifies application protocols
- **Extensibility**: Supports protocol extensions via chunks

## Limitations

- **OS/Library support**: Not as universally supported as TCP/UDP
- **NAT traversal**: Can be challenging (though improving)
- **Firewall compatibility**: Many firewalls don't handle SCTP well
- **Learning curve**: More complex than TCP/UDP

## Summary

SCTP is a powerful transport protocol that bridges the gap between TCP's reliability and UDP's message-orientation while adding unique features like multi-streaming and multi-homing. Multi-streaming eliminates head-of-line blocking by allowing independent message streams within a single association, while multi-homing enables endpoints to use multiple IP addresses for automatic failover and improved reliability.

The protocol is particularly valuable in telecommunications (where it originated for SS7 signaling), WebRTC data channels, and high-availability systems requiring network redundancy. Its message-oriented nature preserves application message boundaries while maintaining reliable, ordered delivery per stream.

Despite its technical advantages, SCTP adoption has been limited by reduced OS/library support compared to TCP/UDP, NAT traversal challenges, and firewall compatibility issues. However, for applications requiring its specific features—particularly those needing multiple independent data streams or network redundancy—SCTP provides capabilities that are difficult or impossible to replicate with TCP or UDP alone. The four-way handshake with cookies also makes it inherently more resistant to certain DoS attacks than TCP's three-way handshake.