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

## Protocol Stack Internals

[51. **IP Routing and Forwarding**](docs/51_IP_Routing_And_Forwarding.md)<br>
Understanding routing tables, forwarding decisions, and policy-based routing

[52. **ARP Protocol Implementation**](docs/52_ARP_Protocol_Implementation.md)<br>
Address Resolution Protocol, ARP cache management, and gratuitous ARP

[53. **ICMP Error Messages**](docs/53_ICMP_Error_Messages.md)<br>
Handling destination unreachable, time exceeded, and parameter problems

[54. **IP Fragmentation and Reassembly**](docs/54_IP_Fragmentation_And_Reassembly.md)<br>
Packet fragmentation mechanics, reassembly buffers, and fragment attacks

[55. **IPv6 Extension Headers**](docs/55_IPv6_Extension_Headers.md)<br>
Hop-by-hop options, routing headers, and security implications

## Server Architecture Patterns

[56. **Iterative vs Concurrent Servers**](docs/56_Iterative_Vs_Concurrent_Servers.md)<br>
Single-threaded, multi-threaded, and multi-process server designs

[57. **Thread Pool Architecture**](docs/57_Thread_Pool_Architecture.md)<br>
Worker thread pools, task queues, and scalability considerations

[58. **Event-Driven Architecture**](docs/58_Event_Driven_Architecture.md)<br>
Reactor and Proactor patterns for high-performance servers

[59. **Fork-based Concurrency**](docs/59_Fork_Based_Concurrency.md)<br>
Pre-fork server models and process management strategies

[60. **Hybrid Server Models**](docs/60_Hybrid_Server_Models.md)<br>
Combining threads, processes, and event loops for optimal performance

## Network Programming Languages

[61. **C Socket Programming**](docs/61_C_Socket_Programming.md)<br>
Low-level socket API, manual memory management, and portability

[62. **C++ Networking (Boost.Asio)**](docs/62_CPP_Networking_Boost_Asio.md)<br>
Asynchronous I/O with Boost.Asio and modern C++ patterns

[63. **Python Socket Programming**](docs/63_Python_Socket_Programming.md)<br>
socket module, asyncio, and Python networking libraries

[64. **Go Networking**](docs/64_Go_Networking.md)<br>
net package, goroutines, and concurrent network programming

[65. **Rust Tokio Ecosystem**](docs/65_Rust_Tokio_Ecosystem.md)<br>
Async/await, tokio runtime, and zero-cost abstractions

## Advanced TCP Features

[66. **TCP Fast Open**](docs/66_TCP_Fast_Open.md)<br>
Reducing connection establishment latency with TFO

[67. **TCP Selective Acknowledgment**](docs/67_TCP_Selective_Acknowledgment.md)<br>
SACK options for efficient retransmission of lost segments

[68. **TCP Window Scaling**](docs/68_TCP_Window_Scaling.md)<br>
Supporting large receive windows for high-bandwidth networks

[69. **TCP Timestamps**](docs/69_TCP_Timestamps.md)<br>
RTT measurement and PAWS (Protection Against Wrapped Sequences)

[70. **ECN (Explicit Congestion Notification)**](docs/70_ECN_Explicit_Congestion_Notification.md)<br>
Congestion signaling without packet loss

## High-Performance Networking

[71. **DPDK (Data Plane Development Kit)**](docs/71_DPDK_Data_Plane_Development_Kit.md)<br>
Kernel bypass for ultra-low latency packet processing

[72. **XDP (eXpress Data Path)**](docs/72_XDP_Express_Data_Path.md)<br>
Early packet processing in the Linux kernel with eBPF

[73. **AF_XDP Sockets**](docs/73_AF_XDP_Sockets.md)<br>
Zero-copy packet I/O between kernel and user space

[74. **RSS and RPS**](docs/74_RSS_And_RPS.md)<br>
Receive Side Scaling and Receive Packet Steering for multi-core systems

[75. **TSO and GSO**](docs/75_TSO_And_GSO.md)<br>
TCP Segmentation Offload and Generic Segmentation Offload

## Network Protocols

[76. **SCTP (Stream Control Transmission Protocol)**](docs/76_SCTP_Stream_Control_Transmission_Protocol.md)<br>
Multi-streaming, multi-homing, and message-oriented transport

[77. **DCCP (Datagram Congestion Control Protocol)**](docs/77_DCCP_Datagram_Congestion_Control_Protocol.md)<br>
Unreliable transport with congestion control

[78. **MPTCP (Multipath TCP)**](docs/78_MPTCP_Multipath_TCP.md)<br>
Using multiple network paths simultaneously for resilience

[79. **gRPC over HTTP/2**](docs/79_gRPC_Over_HTTP2.md)<br>
High-performance RPC framework with protobuf serialization

[80. **HTTP/3 and QUIC**](docs/80_HTTP3_And_QUIC.md)<br>
Modern HTTP over QUIC with improved performance

## Security and Privacy

[81. **TLS 1.3 Features**](docs/81_TLS_1_3_Features.md)<br>
0-RTT, improved handshake, and modern cipher suites

[82. **Certificate Pinning**](docs/82_Certificate_Pinning.md)<br>
Protecting against man-in-the-middle attacks with certificate pinning

[83. **Perfect Forward Secrecy**](docs/83_Perfect_Forward_Secrecy.md)<br>
Ephemeral key exchange and session key protection

[84. **DDoS Mitigation**](docs/84_DDoS_Mitigation.md)<br>
SYN flood protection, rate limiting, and connection limits

[85. **VPN Protocols**](docs/85_VPN_Protocols.md)<br>
OpenVPN, WireGuard, and IPsec implementations

## Network Monitoring and Analysis

[86. **Packet Capture and Analysis**](docs/86_Packet_Capture_And_Analysis.md)<br>
Deep packet inspection, protocol analysis, and traffic patterns

[87. **Network Metrics Collection**](docs/87_Network_Metrics_Collection.md)<br>
Bandwidth, latency, packet loss, and jitter measurement

[88. **Flow Monitoring (NetFlow/sFlow)**](docs/88_Flow_Monitoring_NetFlow_sFlow.md)<br>
Network traffic flow analysis and visualization

[89. **Application Layer Tracing**](docs/89_Application_Layer_Tracing.md)<br>
Distributed tracing, correlation IDs, and observability

[90. **Network Performance Profiling**](docs/90_Network_Performance_Profiling.md)<br>
Identifying bottlenecks and optimizing network stack performance

## Mobile and Wireless

[91. **Mobile Network Optimization**](docs/91_Mobile_Network_Optimization.md)<br>
Handling high latency, packet loss, and connection switching

[92. **WiFi Direct and P2P**](docs/92_WiFi_Direct_And_P2P.md)<br>
Peer-to-peer networking without infrastructure

[93. **Bluetooth Networking**](docs/93_Bluetooth_Networking.md)<br>
RFCOMM, L2CAP, and BLE networking protocols

[94. **Cellular Network Programming**](docs/94_Cellular_Network_Programming.md)<br>
LTE/5G data connections and network selection

[95. **Network Switching Detection**](docs/95_Network_Switching_Detection.md)<br>
Handling transitions between WiFi, cellular, and Ethernet

## Cloud and Distributed Systems

[96. **Service Mesh Networking**](docs/96_Service_Mesh_Networking.md)<br>
Istio, Linkerd, and sidecar proxy patterns

[97. **Network Policies in Kubernetes**](docs/97_Network_Policies_In_Kubernetes.md)<br>
Pod networking, network policies, and CNI plugins

[98. **Edge Computing Networking**](docs/98_Edge_Computing_Networking.md)<br>
Low-latency communication between edge nodes and cloud

[99. **CDN Integration**](docs/99_CDN_Integration.md)<br>
Content delivery networks, caching strategies, and edge servers

[100. **Future of Network Protocols**](docs/100_Future_Of_Network_Protocols.md)<br>
HTTP/4, BBR congestion control, and emerging networking technologies