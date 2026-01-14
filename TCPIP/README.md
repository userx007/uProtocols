# TCP/IP Essential Topics 

## Fundamentals

[01. **Socket Programming Basics**](docs/01_Socket_Programming_Basics.md)<br>
Understanding socket creation, binding, and basic client-server architecture

[02. **Network Byte Order**](docs/02_Network_Byte_Order.md)<br>
Handling endianness with htons, htonl, ntohs, ntohl for portable network code

[03. **IPv4 vs IPv6**](docs/03_IPv4_vs_IPv6.md)<br>
Address families, structures, and dual-stack implementation strategies

[04. **Socket Options**](docs/04_Socket_Options.md)<br>
SO_REUSEADDR, SO_KEEPALIVE, TCP_NODELAY and other critical socket configurations

[05. **Non-blocking I/O**](docs/05_Non_Blocking_IO.md)<br>
fcntl, O_NONBLOCK flags, and handling EAGAIN/EWOULDBLOCK errors

## Advanced Socket Programming

[06. **select() System Call**](docs/06_Select_System_Call.md)<br>
Multiplexing I/O with select and fd_set management

[07. **poll() and ppoll()**](docs/07_Poll_And_Ppoll.md)<br>
Modern alternative to select with better scalability

[08. **epoll() on Linux**](docs/08_Epoll_On_Linux.md)<br>
High-performance event notification mechanism for Linux systems

[09. **kqueue() on BSD/macOS**](docs/09_Kqueue_On_BSD_macOS.md)<br>
Kernel event notification interface for BSD-based systems

[10. **io_uring**](docs/10_Io_Uring.md)<br>
Next-generation asynchronous I/O interface for Linux

## TCP Protocol Details

[11. **TCP Connection Establishment**](docs/11_TCP_Connection_Establishment.md)<br>
Three-way handshake implementation and state transitions

[12. **TCP Connection Termination**](docs/12_TCP_Connection_Termination.md)<br>
Four-way handshake, TIME_WAIT state, and graceful shutdown

[13. **TCP Flow Control**](docs/13_TCP_Flow_Control.md)<br>
Window management, sliding window protocol, and buffer sizing

[14. **TCP Congestion Control**](docs/14_TCP_Congestion_Control.md)<br>
Slow start, congestion avoidance, fast retransmit, and fast recovery

[15. **TCP Keepalive**](docs/15_TCP_Keepalive.md)<br>
Detecting dead connections and configuring keepalive parameters

[16. **Nagle's Algorithm**](docs/16_Nagles_Algorithm.md)<br>
Understanding and controlling packet coalescing with TCP_NODELAY

[17. **TCP State Machine**](docs/17_TCP_State_Machine.md)<br>
Complete state transitions and their implications for application design

## UDP Protocol

[18. **UDP Socket Programming**](docs/18_UDP_Socket_Programming.md)<br>
Connectionless communication with sendto/recvfrom

[19. **UDP Broadcast and Multicast**](docs/19_UDP_Broadcast_And_Multicast.md)<br>
Implementing broadcast and multicast communication patterns

[20. **UDP Reliability Patterns**](docs/20_UDP_Reliability_Patterns.md)<br>
Building reliable protocols on top of UDP with ACKs and retransmission

## Raw Sockets and Packet Crafting

[21. **Raw Sockets**](docs/21_Raw_Sockets.md)<br>
Creating and using raw sockets for custom protocol implementation

[22. **ICMP Implementation**](docs/22_ICMP_Implementation.md)<br>
Building ping and traceroute tools using raw sockets

[23. **Packet Filtering with BPF**](docs/23_Packet_Filtering_With_BPF.md)<br>
Berkeley Packet Filter for efficient packet capture and filtering

[24. **libpcap Programming**](docs/24_Libpcap_Programming.md)<br>
Packet capture library for network analysis tools

## Network Security

[25. **TLS/SSL with OpenSSL**](docs/25_TLS_SSL_With_OpenSSL.md)<br>
Implementing secure connections using OpenSSL library

[26. **Certificate Validation**](docs/26_Certificate_Validation.md)<br>
Proper certificate chain validation and hostname verification

[27. **Cryptographic Best Practices**](docs/27_Cryptographic_Best_Practices.md)<br>
Secure random number generation, key derivation, and cipher selection

[28. **Authentication Mechanisms**](docs/28_Authentication_Mechanisms.md)<br>
Token-based auth, mutual TLS, and secure credential handling

## Protocol Implementation

[29. **HTTP/1.1 Implementation**](docs/29_HTTP_1_1_Implementation.md)<br>
Building a basic HTTP client and server from scratch

[30. **HTTP/2 Fundamentals**](docs/30_HTTP_2_Fundamentals.md)<br>
Multiplexing, server push, and binary framing layer

[31. **WebSocket Protocol**](docs/31_WebSocket_Protocol.md)<br>
Implementing full-duplex communication over TCP

[32. **DNS Resolution**](docs/32_DNS_Resolution.md)<br>
getaddrinfo, custom DNS queries, and asynchronous resolution

[33. **Protocol Buffers**](docs/33_Protocol_Buffers.md)<br>
Efficient binary serialization for network protocols

## Performance and Optimization

[34. **Zero-Copy Techniques**](docs/34_Zero_Copy_Techniques.md)<br>
sendfile, splice, and memory-mapped I/O for performance

[35. **Buffer Management**](docs/35_Buffer_Management.md)<br>
Ring buffers, scatter-gather I/O, and optimal buffer sizing

[36. **Connection Pooling**](docs/36_Connection_Pooling.md)<br>
Reusing connections to reduce overhead and improve throughput

[37. **Load Balancing Strategies**](docs/37_Load_Balancing_Strategies.md)<br>
Round-robin, least connections, and consistent hashing

[38. **Network Latency Optimization**](docs/38_Network_Latency_Optimization.md)<br>
Reducing RTT, TCP tuning, and application-level optimizations

## Error Handling and Debugging

[39. **Error Code Handling**](docs/39_Error_Code_Handling.md)<br>
Proper errno handling, EINTR, EAGAIN, and recovery strategies

[40. **Timeout Management**](docs/40_Timeout_Management.md)<br>
SO_RCVTIMEO, SO_SNDTIMEO, and application-level timeouts

[41. **Network Debugging Tools**](docs/41_Network_Debugging_Tools.md)<br>
tcpdump, wireshark, netstat, ss, and strace for troubleshooting

[42. **Connection Leak Detection**](docs/42_Connection_Leak_Detection.md)<br>
Finding and preventing file descriptor and socket leaks

## Advanced Topics

[43. **Async/Await in Rust**](docs/43_Async_Await_In_Rust.md)<br>
Tokio, async-std, and asynchronous network programming patterns

[44. **QUIC Protocol**](docs/44_QUIC_Protocol.md)<br>
UDP-based transport with built-in TLS and multiplexing

[45. **Network Address Translation**](docs/45_Network_Address_Translation.md)<br>
NAT traversal, STUN, TURN, and hole punching techniques

[46. **Quality of Service**](docs/46_Quality_Of_Service.md)<br>
TOS/DSCP fields, traffic shaping, and priority queuing

[47. **Jumbo Frames and MTU**](docs/47_Jumbo_Frames_And_MTU.md)<br>
Path MTU discovery and handling fragmentation

[48. **Unix Domain Sockets**](docs/48_Unix_Domain_Sockets.md)<br>
IPC using local sockets with file system paths

[49. **SO_LINGER and Connection Cleanup**](docs/49_SO_LINGER_And_Connection_Cleanup.md)<br>
Controlling socket closure behavior and data flushing

[50. **Container Networking**](docs/50_Container_Networking.md)<br>
Virtual interfaces, network namespaces, and overlay networks