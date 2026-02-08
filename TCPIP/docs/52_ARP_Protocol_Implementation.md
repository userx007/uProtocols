# ARP Protocol Implementation

## Overview

**Address Resolution Protocol (ARP)** is a fundamental protocol in the TCP/IP suite that maps network layer addresses (IP addresses) to link layer addresses (MAC addresses). Operating at the data link layer (Layer 2), ARP enables communication between devices on the same local network by resolving logical IP addresses to physical hardware addresses.

## How ARP Works

### Basic Operation

1. **ARP Request**: When a device needs to send data to an IP address on the local network but doesn't know the MAC address, it broadcasts an ARP request to all devices on the network segment
2. **ARP Reply**: The device with the matching IP address responds with its MAC address
3. **Cache Storage**: The requesting device stores this mapping in its ARP cache for future use

### ARP Packet Structure

```
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Hardware Type         |         Protocol Type         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| HW Addr Len   | Proto Addr Len|          Operation            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Sender Hardware Address                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Sender Protocol Address                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Target Hardware Address                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Target Protocol Address                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Key Fields:**
- Hardware Type: 1 for Ethernet
- Protocol Type: 0x0800 for IPv4
- Operation: 1 (ARP Request), 2 (ARP Reply)

## ARP Cache Management

The ARP cache is a temporary storage of IP-to-MAC address mappings with the following characteristics:

- **Dynamic Entries**: Learned through ARP requests/replies, expire after timeout (typically 60-300 seconds)
- **Static Entries**: Manually configured, don't expire
- **Cache Poisoning Prevention**: Implementations should validate and limit cache updates

## Gratuitous ARP

**Gratuitous ARP** is an ARP packet where the sender's IP address is the same as the target IP address. Uses include:

1. **IP Conflict Detection**: Check if another device is using the same IP
2. **Cache Update**: Announce MAC address changes (e.g., during failover)
3. **Unsolicited Cache Population**: Pre-populate ARP caches of other devices


## What's Included:

**📖 ARP_Protocol_Documentation.md** - Complete technical guide covering:
- Protocol fundamentals and operation flow
- Detailed packet structure with diagrams
- ARP cache management strategies
- Gratuitous ARP uses and implementation
- Security considerations (ARP spoofing, poisoning)
- Performance tuning and best practices

**💻 arp_implementation.c** - Full C implementation featuring:
- Raw socket programming for packet transmission
- ARP request/reply creation and parsing
- Complete cache management with dynamic/static entries
- Gratuitous ARP generation
- Cache cleanup and timeout handling
- ~400 lines with extensive comments
[arp_implementation.c](../src/c/arp_implementation.c)<br>

**🦀 arp_implementation.rs** - Modern Rust implementation with:
- Type-safe packet structures
- Memory-safe cache operations
- ARP handler for packet processing
- Built-in spoof detection
- Comprehensive unit tests
- ~600 lines of idiomatic Rust
[arp_implementation.rs](../src/rust/arp_implementation.rs)<br>


**📦 Cargo.toml** - Rust project configuration ready to compile
[Cargo.toml](../src/rust/Cargo.toml)<br>


**📚 README.md** - Quick start guide with:
- Compilation instructions for both languages
- Usage examples and code snippets
- Testing procedures with tcpdump/arping
- Troubleshooting common issues
- Security warnings and best practices
[README.md](../src/README.md)<br>


Both implementations require root privileges for raw socket access and include production-ready features like error handling, cache management, and security validation. The C version is more lightweight, while the Rust version provides memory safety and modern abstractions.

---

# ARP Protocol Implementation - Complete Guide

## Table of Contents
1. [Introduction](#introduction)
2. [ARP Fundamentals](#arp-fundamentals)
3. [Packet Structure](#packet-structure)
4. [ARP Operations](#arp-operations)
5. [ARP Cache Management](#arp-cache-management)
6. [Gratuitous ARP](#gratuitous-arp)
7. [Security Considerations](#security-considerations)
8. [Implementation Details](#implementation-details)
9. [Code Examples](#code-examples)
10. [Summary](#summary)

---

## Introduction

**Address Resolution Protocol (ARP)** is a critical network protocol defined in RFC 826 that enables communication on local networks by mapping network layer addresses (IP addresses) to data link layer addresses (MAC addresses). Without ARP, devices would be unable to communicate even though they have IP connectivity.

### Key Characteristics
- **Protocol Layer**: Data Link Layer (Layer 2) / Network Layer (Layer 3) boundary
- **Transport**: Operates directly over Ethernet (EtherType 0x0806)
- **Scope**: Local network segment only (non-routable)
- **Type**: Request-Reply protocol
- **RFC**: RFC 826 (original), RFC 5227 (IPv4 ACD)

---

## ARP Fundamentals

### Why ARP is Necessary

In Ethernet networks:
- **IP addresses** are logical addresses used for routing across networks
- **MAC addresses** are physical addresses used for local delivery
- Network interface cards (NICs) only understand MAC addresses

When Host A (192.168.1.10) wants to send data to Host B (192.168.1.20):
1. Host A knows only the IP address of Host B
2. Host A needs the MAC address to construct the Ethernet frame
3. Host A uses ARP to resolve the IP → MAC mapping

### ARP Process Flow

```
Host A (192.168.1.10)                    Host B (192.168.1.20)
    |                                            |
    |  ARP Request (Broadcast)                  |
    |  "Who has 192.168.1.20?"                  |
    |  "Tell 192.168.1.10"                      |
    |------------------------------------------->|
    |                                            |
    |  ARP Reply (Unicast)                      |
    |  "192.168.1.20 is at MAC: BB:BB:BB:BB:BB:BB"
    |<-------------------------------------------|
    |                                            |
    [Cache: 192.168.1.20 → BB:BB:BB:BB:BB:BB]
```

---

## Packet Structure

### Ethernet Frame with ARP Payload

```
+------------------+------------------+------------------+
| Ethernet Header  |   ARP Packet     |  Frame Check     |
|   (14 bytes)     |   (28 bytes)     |  Sequence        |
+------------------+------------------+------------------+
```

### ARP Packet Format (28 bytes for Ethernet/IPv4)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      Hardware Type (HTYPE)    |      Protocol Type (PTYPE)    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| HW Addr Len   | Proto Addr Len|         Operation (OPER)      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                Sender Hardware Address (SHA)                  |
|                         (6 bytes)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                Sender Protocol Address (SPA)                  |
|                         (4 bytes)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                Target Hardware Address (THA)                  |
|                         (6 bytes)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                Target Protocol Address (TPA)                  |
|                         (4 bytes)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Field Descriptions

| Field | Size | Description | Common Value |
|-------|------|-------------|--------------|
| Hardware Type | 2 bytes | Link layer protocol type | 1 (Ethernet) |
| Protocol Type | 2 bytes | Network layer protocol | 0x0800 (IPv4) |
| HW Addr Length | 1 byte | Length of hardware address | 6 (MAC address) |
| Proto Addr Length | 1 byte | Length of protocol address | 4 (IPv4) |
| Operation | 2 bytes | ARP operation code | 1 (Request), 2 (Reply) |
| Sender HW Addr | 6 bytes | MAC address of sender | Variable |
| Sender Proto Addr | 4 bytes | IP address of sender | Variable |
| Target HW Addr | 6 bytes | MAC address of target | 00:00:00:00:00:00 in request |
| Target Proto Addr | 4 bytes | IP address being queried | Variable |

---

## ARP Operations

### 1. ARP Request

**Purpose**: Discover the MAC address for a known IP address

**Characteristics**:
- **Destination MAC**: Broadcast (FF:FF:FF:FF:FF:FF)
- **Operation Code**: 1
- **Target Hardware Address**: 00:00:00:00:00:00 (unknown)
- **Delivery**: Broadcast to all hosts on local segment

**Example**:
```
Operation: Request (1)
Sender MAC: AA:AA:AA:AA:AA:AA
Sender IP: 192.168.1.10
Target MAC: 00:00:00:00:00:00
Target IP: 192.168.1.20
```

### 2. ARP Reply

**Purpose**: Respond with MAC address information

**Characteristics**:
- **Destination MAC**: Unicast (requester's MAC)
- **Operation Code**: 2
- **Target Hardware Address**: Filled with requester's MAC
- **Delivery**: Unicast to requesting host

**Example**:
```
Operation: Reply (2)
Sender MAC: BB:BB:BB:BB:BB:BB
Sender IP: 192.168.1.20
Target MAC: AA:AA:AA:AA:AA:AA
Target IP: 192.168.1.10
```

### 3. Probe (ARP Probe)

**Purpose**: Check if an IP address is already in use (duplicate detection)

**Characteristics**:
- Sender IP: 0.0.0.0
- Target IP: IP being tested
- Used during DHCP or static configuration

---

## ARP Cache Management

### Cache Purpose

The ARP cache (ARP table) stores recent IP-to-MAC mappings to avoid:
- Excessive network traffic from repeated ARP requests
- Communication delays
- Network congestion

### Cache Entry Types

#### 1. Dynamic Entries
- **Source**: Learned from ARP replies
- **Lifetime**: Temporary (typically 60-300 seconds)
- **Behavior**: Automatically expire and are removed
- **Update**: Can be refreshed by new ARP traffic

#### 2. Static Entries
- **Source**: Manually configured by administrator
- **Lifetime**: Permanent (until explicitly removed)
- **Behavior**: Never expire
- **Purpose**: Critical devices (gateways, servers)

### Cache States

```
[INCOMPLETE] → ARP request sent, waiting for reply
[REACHABLE]  → Entry valid and recently confirmed
[STALE]      → Entry old but not yet expired
[DELAY]      → Entry being verified
[PROBE]      → Actively probing for reachability
```

### Cache Management Operations

#### 1. Addition
- Entry added when ARP reply received
- Timestamp recorded for expiration tracking

#### 2. Lookup
- Check if IP exists in cache
- Verify entry hasn't expired (for dynamic entries)
- Return MAC address if valid

#### 3. Update
- Overwrite existing entry with new MAC
- Reset timestamp
- Can be triggered by gratuitous ARP

#### 4. Deletion
- Remove expired dynamic entries
- Manual removal of static entries

#### 5. Timeout Management
```c
// Typical timeout values
#define ARP_CACHE_TIMEOUT_INCOMPLETE  3     // seconds
#define ARP_CACHE_TIMEOUT_REACHABLE   300   // 5 minutes
#define ARP_CACHE_TIMEOUT_STALE       900   // 15 minutes
```

### Cache Size Considerations

- **Small cache**: Frequent ARP requests, network overhead
- **Large cache**: Memory usage, stale entries
- **Typical size**: 256-1024 entries

---

## Gratuitous ARP

### Definition

A **Gratuitous ARP** is an ARP packet where:
- Sender IP address = Target IP address
- Typically sent as broadcast
- Can be either Request or Reply operation

### Purposes

#### 1. IP Conflict Detection
Before using an IP address, a host sends gratuitous ARP to check if another host is already using it.

```
If no response → IP available
If response → IP conflict detected
```

#### 2. Cache Update Announcement
When a host's MAC address changes (hardware replacement, virtual machine migration), it announces the change.

```
"My IP (192.168.1.100) is now at MAC: NEW:MA:C:AD:DR:ES"
```

#### 3. High Availability / Failover
Virtual IP (VIP) moves between redundant systems:

```
Primary Server Fails
    ↓
Backup Takes Over VIP
    ↓
Sends Gratuitous ARP
    ↓
All Clients Update Cache
```

#### 4. Network Monitoring
Some systems send periodic gratuitous ARPs to maintain presence.

### Gratuitous ARP Example

```
Operation: Reply (2)  [or Request (1)]
Sender MAC: AA:BB:CC:DD:EE:FF
Sender IP: 192.168.1.100
Target MAC: 00:00:00:00:00:00  [or FF:FF:FF:FF:FF:FF]
Target IP: 192.168.1.100  [SAME AS SENDER]
Destination: Broadcast
```

### Implementation Considerations

```rust
fn is_gratuitous_arp(packet: &ArpPacket) -> bool {
    packet.sender_ip == packet.target_ip
}

fn send_gratuitous_arp(interface: &Interface, ip: Ipv4Addr, mac: MacAddr) {
    let garp = ArpPacket {
        operation: ArpOperation::Reply,
        sender_mac: mac,
        sender_ip: ip,
        target_mac: MacAddr::zero(), // or broadcast
        target_ip: ip,  // Same as sender!
    };
    broadcast(interface, &garp);
}
```

---

## Security Considerations

### 1. ARP Spoofing / ARP Poisoning

**Attack**: Malicious host sends fake ARP replies to associate attacker's MAC with victim's IP.

**Impact**:
- Man-in-the-middle attacks
- Traffic interception
- Denial of service

**Example**:
```
Attacker sends:
  "192.168.1.1 (gateway) is at ATTACKER:MAC:ADDRESS"
  
Victim's cache now poisoned:
  192.168.1.1 → ATTACKER:MAC (instead of real gateway)
  
All traffic to gateway → routed to attacker
```

**Mitigation**:
- Static ARP entries for critical devices
- ARP inspection (Dynamic ARP Inspection - DAI)
- Network monitoring for ARP anomalies
- Encryption (TLS/SSL) for sensitive data

### 2. ARP Request Floods

**Attack**: Overwhelming network with ARP requests.

**Mitigation**:
- Rate limiting
- ARP request filtering

### 3. ARP Cache Overflow

**Attack**: Fill target's ARP cache with bogus entries.

**Mitigation**:
- Cache size limits
- Entry validation

### Defense Implementation Example

```rust
pub struct ArpDefender {
    trusted_mappings: HashMap<Ipv4Addr, MacAddress>,
    request_rate_limiter: RateLimiter,
}

impl ArpDefender {
    pub fn validate_arp(&self, packet: &ArpPacket) -> bool {
        // Check against trusted mappings
        if let Some(&trusted_mac) = self.trusted_mappings.get(&packet.sender_ip) {
            if packet.sender_mac != trusted_mac {
                log_alert("ARP spoofing detected!");
                return false;
            }
        }
        
        // Rate limit requests
        if !self.request_rate_limiter.check() {
            log_alert("ARP flood detected!");
            return false;
        }
        
        true
    }
}
```

### 4. Gratuitous ARP Abuse

**Risk**: Malicious gratuitous ARPs can poison caches.

**Mitigation**:
- Validate gratuitous ARPs against known mappings
- Require confirmation for cache updates
- Log all gratuitous ARP events

---

## Implementation Details

### Raw Socket Requirements

ARP operates at Layer 2, requiring raw socket access:

**Linux**:
```c
int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
// Requires CAP_NET_RAW capability or root privileges
```

**Privileges**:
- Root access typically required
- Or CAP_NET_RAW capability
- Or BPF (Berkeley Packet Filter) on some systems

### Sending ARP Packets

#### Step 1: Create Socket
```c
int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
```

#### Step 2: Get Interface Index
```c
struct ifreq ifr;
strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);
ioctl(sockfd, SIOCGIFINDEX, &ifr);
```

#### Step 3: Construct Packet
```c
struct arp_packet packet;
// Fill Ethernet header
// Fill ARP header
```

#### Step 4: Send
```c
struct sockaddr_ll addr;
addr.sll_family = AF_PACKET;
addr.sll_ifindex = ifr.ifr_ifindex;
sendto(sockfd, &packet, sizeof(packet), 0, 
       (struct sockaddr*)&addr, sizeof(addr));
```

### Receiving ARP Packets

#### Step 1: Create Listening Socket
```c
int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
```

#### Step 2: Bind to Interface
```c
struct sockaddr_ll addr;
addr.sll_family = AF_PACKET;
addr.sll_protocol = htons(ETH_P_ARP);
addr.sll_ifindex = ifr.ifr_ifindex;
bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
```

#### Step 3: Receive Loop
```c
while (1) {
    struct arp_packet packet;
    recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
    process_arp_packet(&packet);
}
```

### Byte Order Considerations

Network byte order is **big-endian**. Host byte order may differ.

```c
// Always convert multi-byte fields
packet.hw_type = htons(1);           // host to network short
packet.operation = htons(ARP_REQUEST);

// When reading
uint16_t op = ntohs(packet.operation); // network to host short
```

---

## Performance Considerations

### 1. Cache Efficiency

```
Cache Hit Ratio = (Cache Hits) / (Total Lookups)

Good: > 95%
Acceptable: 80-95%
Poor: < 80%
```

### 2. Timeout Tuning

- **Too short**: Excessive ARP traffic
- **Too long**: Stale entries, incorrect routing
- **Recommended**: 300 seconds (5 minutes)

### 3. Batch Operations

Process multiple ARP packets in batches to reduce context switches:

```rust
fn process_arp_batch(packets: &[ArpPacket]) {
    for packet in packets {
        handle_arp(packet);
    }
}
```

### 4. Memory Management

Use efficient data structures:
- **Hash table**: O(1) lookup
- **Linked list**: Easy expiration management
- **Hybrid**: Hash table with LRU list

---

## Testing and Debugging

### View ARP Cache (Linux)
```bash
# Display cache
ip neighbor show
arp -n

# Add static entry
arp -s 192.168.1.100 AA:BB:CC:DD:EE:FF

# Delete entry
arp -d 192.168.1.100

# Clear all dynamic entries
ip -s -s neigh flush all
```

### Capture ARP Traffic
```bash
# Using tcpdump
tcpdump -i eth0 arp -vv

# Using Wireshark filter
arp

# Specific operation
arp.opcode == 1  # Requests only
arp.opcode == 2  # Replies only
```

### Send Test ARP (arping)
```bash
# Send ARP request
arping -I eth0 192.168.1.1

# Send gratuitous ARP
arping -A -I eth0 192.168.1.10
```

---

## Summary

### Key Takeaways

1. **Purpose**: ARP resolves IP addresses to MAC addresses on local networks

2. **Operation**: 
   - Request broadcast: "Who has this IP?"
   - Reply unicast: "I have that IP, here's my MAC"

3. **Cache Management**:
   - Stores mappings temporarily (dynamic) or permanently (static)
   - Reduces network overhead
   - Requires timeout and cleanup mechanisms

4. **Gratuitous ARP**:
   - Announces presence and changes
   - Detects IP conflicts
   - Enables high availability

5. **Security**:
   - Vulnerable to spoofing and poisoning
   - Requires monitoring and validation
   - Static entries for critical devices

6. **Implementation**:
   - Requires raw socket access
   - Must handle byte ordering
   - Should include cache management
   - Must consider security

### Best Practices

✅ **DO**:
- Implement cache timeout mechanisms
- Validate incoming ARP packets
- Use static entries for critical infrastructure
- Log unusual ARP activity
- Rate-limit ARP processing
- Handle gratuitous ARP appropriately

❌ **DON'T**:
- Trust all ARP replies blindly
- Keep cache entries indefinitely
- Ignore gratuitous ARP
- Process ARP without rate limiting
- Forget byte order conversion
- Run without proper privileges

### Real-World Applications

1. **Network Switches**: Learn MAC addresses and build forwarding tables
2. **Firewalls**: Track and validate network mappings
3. **Load Balancers**: Use gratuitous ARP for failover
4. **Virtual Machines**: Announce presence on network migration
5. **Network Monitoring**: Detect rogue devices and attacks
6. **DHCP Servers**: Check IP availability before assignment

### Further Reading

- RFC 826: An Ethernet Address Resolution Protocol
- RFC 5227: IPv4 Address Conflict Detection
- RFC 5944: IP Mobility Support for IPv4, Revised
- Linux kernel source: net/ipv4/arp.c
- "TCP/IP Illustrated, Volume 1" by W. Richard Stevens

---

## Appendix: Quick Reference

### ARP Packet Operations
| Code | Operation | Description |
|------|-----------|-------------|
| 1 | REQUEST | Who has IP X? |
| 2 | REPLY | IP X is at MAC Y |
| 3 | RARP REQUEST | Reverse ARP (obsolete) |
| 4 | RARP REPLY | Reverse ARP reply |

### Common Hardware Types
| Code | Type |
|------|------|
| 1 | Ethernet |
| 6 | IEEE 802 Networks |
| 15 | Frame Relay |
| 20 | Serial Line |

### Typical Cache Timeouts
| System | Default Timeout |
|--------|-----------------|
| Linux | 300 seconds (5 min) |
| Windows | 120 seconds (2 min) |
| macOS | 1200 seconds (20 min) |
| Cisco IOS | 14400 seconds (4 hours) |

### Socket Types
| OS | Socket Type | Header |
|----|-------------|--------|
| Linux | AF_PACKET | <linux/if_packet.h> |
| BSD | AF_LINK | <net/if_dl.h> |
| Windows | Npcap/WinPcap | pcap.h |

---

*This document provides a comprehensive guide to ARP protocol implementation. The accompanying C/C++ and Rust code examples demonstrate practical implementations of the concepts discussed.*