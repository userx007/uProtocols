# IPv6 Extension Headers: A Comprehensive Guide

## Detailed Description

### Overview

IPv6 Extension Headers provide a flexible mechanism for extending the IPv6 protocol without modifying the base header. Unlike IPv4's limited options field, IPv6 uses a chain of extension headers between the main IPv6 header and the upper-layer protocol (like TCP or UDP). This modular approach allows for cleaner protocol design and efficient processing.

### Structure

The IPv6 header chain works as follows:
```
[IPv6 Base Header] → [Extension Header 1] → [Extension Header 2] → ... → [Upper Layer Protocol]
```

Each header contains a "Next Header" field indicating what follows. Extension headers should appear in this specific order:
1. **Hop-by-Hop Options** (0)
2. **Destination Options** (60) - processed by first destination
3. **Routing Header** (43)
4. **Fragment Header** (44)
5. **Authentication Header (AH)** (51)
6. **Encapsulating Security Payload (ESP)** (50)
7. **Destination Options** (60) - processed by final destination
8. **Upper Layer** (TCP=6, UDP=17, ICMPv6=58, etc.)

### Key Extension Headers

#### 1. Hop-by-Hop Options Header (Type 0)

**Purpose**: Contains options that must be examined by every node along the packet's delivery path.

**Structure**:
- Next Header (8 bits)
- Header Length (8 bits)
- Options (variable)

**Common Options**:
- **Router Alert**: Notifies routers to examine packet more closely
- **Jumbo Payload**: Supports packets larger than 65,535 bytes
- **RPL Option**: For routing in low-power and lossy networks

**Security Concern**: Since every router must process these options, they can be exploited for DoS attacks by forcing routers to perform complex processing.

#### 2. Routing Header (Type 43)

**Purpose**: Specifies one or more intermediate nodes to visit before reaching the destination.

**Types**:
- **Type 0** (Deprecated): Original routing header - DEPRECATED due to security vulnerabilities
- **Type 2**: Used for Mobile IPv6
- **Type 3**: RPL Source Route Header
- **Type 4**: Segment Routing Header (SRH)

**Security Issues**:
- **Type 0 Amplification Attacks**: Allowed packets to be bounced between nodes exponentially
- **Traffic Amplification**: Used in DDoS attacks
- **Firewall Bypass**: Could route around security controls

#### 3. Fragment Header (Type 44)

Used when a packet is too large for the path MTU. Unlike IPv4, only the source can fragment in IPv6.

#### 4. Destination Options Header (Type 60)

Contains options examined only by the destination node(s).

### Security Implications

1. **Processing Overhead**: Complex extension headers can overwhelm routers
2. **Fragmentation Attacks**: Overlapping fragments, tiny fragments
3. **Routing Header Exploits**: Especially deprecated Type 0
4. **Firewall Evasion**: Extension headers can obscure the actual payload
5. **Deep Packet Inspection Challenges**: Makes filtering harder
6. **Resource Exhaustion**: Malformed or excessive headers

**Mitigations**:
- Drop packets with deprecated routing headers
- Limit extension header processing
- Implement rate limiting
- Use stateful inspection
- Drop packets with excessive extension header chains

---

## Programming Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>

// IPv6 Base Header Structure (already defined in netinet/ip6.h)
// struct ip6_hdr {
//     union {
//         struct ip6_hdrctl {
//             uint32_t ip6_un1_flow;
//             uint16_t ip6_un1_plen;
//             uint8_t  ip6_un1_nxt;
//             uint8_t  ip6_un1_hlim;
//         } ip6_un1;
//         uint8_t ip6_un2_vfc;
//     } ip6_ctlun;
//     struct in6_addr ip6_src;
//     struct in6_addr ip6_dst;
// };

// Extension Header Types
#define IPPROTO_HOPOPTS    0   // Hop-by-hop options
#define IPPROTO_ROUTING    43  // Routing header
#define IPPROTO_FRAGMENT   44  // Fragment header
#define IPPROTO_DSTOPTS    60  // Destination options

// Generic Extension Header
typedef struct {
    uint8_t next_header;
    uint8_t hdr_len;        // Length in 8-byte units, minus 1
    uint8_t options[0];     // Variable length options
} ipv6_ext_hdr_t;

// Hop-by-Hop Options Header
typedef struct {
    uint8_t next_header;
    uint8_t hdr_len;
    uint8_t options[6];     // Padded to 8 bytes
} ipv6_hopbyhop_t;

// Routing Header (Generic)
typedef struct {
    uint8_t next_header;
    uint8_t hdr_len;
    uint8_t routing_type;
    uint8_t segments_left;
    uint8_t data[0];        // Type-specific data
} ipv6_routing_hdr_t;

// Routing Header Type 2 (Mobile IPv6)
typedef struct {
    uint8_t next_header;
    uint8_t hdr_len;
    uint8_t routing_type;    // 2
    uint8_t segments_left;
    uint32_t reserved;
    struct in6_addr home_address;
} ipv6_routing_type2_t;

// Fragment Header
typedef struct {
    uint8_t next_header;
    uint8_t reserved;
    uint16_t frag_offset_flags;  // 13 bits offset, 2 reserved, 1 M flag
    uint32_t identification;
} ipv6_fragment_hdr_t;

// TLV Option Format
typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t value[0];
} ipv6_option_t;

// Router Alert Option
typedef struct {
    uint8_t type;      // 5
    uint8_t length;    // 2
    uint16_t value;    // Alert value
} ipv6_router_alert_t;

// Function to create a Hop-by-Hop header with Router Alert
void create_hopbyhop_router_alert(uint8_t *buffer, uint8_t next_hdr, 
                                   uint16_t alert_value) {
    ipv6_hopbyhop_t *hbh = (ipv6_hopbyhop_t *)buffer;
    
    hbh->next_header = next_hdr;
    hbh->hdr_len = 0;  // (8 bytes total - 8) / 8 = 0
    
    // Router Alert option
    ipv6_router_alert_t *alert = (ipv6_router_alert_t *)hbh->options;
    alert->type = 5;
    alert->length = 2;
    alert->value = htons(alert_value);
    
    // PadN option to fill to 8 bytes (2 bytes needed)
    hbh->options[4] = 1;  // PadN type
    hbh->options[5] = 0;  // PadN length (0 means 2 bytes total)
}

// Function to parse extension headers
int parse_extension_headers(const uint8_t *packet, size_t packet_len) {
    const struct ip6_hdr *ip6 = (const struct ip6_hdr *)packet;
    const uint8_t *ptr = packet + sizeof(struct ip6_hdr);
    uint8_t next_hdr = ip6->ip6_nxt;
    
    printf("IPv6 Packet Analysis:\n");
    printf("Source: ");
    char src[INET6_ADDRSTRLEN], dst[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &ip6->ip6_src, src, INET6_ADDRSTRLEN);
    inet_ntop(AF_INET6, &ip6->ip6_dst, dst, INET6_ADDRSTRLEN);
    printf("%s\n", src);
    printf("Destination: %s\n", dst);
    printf("\nExtension Headers:\n");
    
    while (ptr < packet + packet_len) {
        switch (next_hdr) {
            case IPPROTO_HOPOPTS: {
                printf("- Hop-by-Hop Options Header\n");
                const ipv6_ext_hdr_t *hbh = (const ipv6_ext_hdr_t *)ptr;
                size_t hdr_size = (hbh->hdr_len + 1) * 8;
                printf("  Header Length: %zu bytes\n", hdr_size);
                next_hdr = hbh->next_header;
                ptr += hdr_size;
                break;
            }
            
            case IPPROTO_ROUTING: {
                printf("- Routing Header\n");
                const ipv6_routing_hdr_t *rh = (const ipv6_routing_hdr_t *)ptr;
                printf("  Routing Type: %d\n", rh->routing_type);
                printf("  Segments Left: %d\n", rh->segments_left);
                
                // Security check
                if (rh->routing_type == 0) {
                    printf("  WARNING: Deprecated Type 0 Routing Header detected!\n");
                }
                
                size_t hdr_size = (rh->hdr_len + 1) * 8;
                next_hdr = rh->next_header;
                ptr += hdr_size;
                break;
            }
            
            case IPPROTO_FRAGMENT: {
                printf("- Fragment Header\n");
                const ipv6_fragment_hdr_t *fh = (const ipv6_fragment_hdr_t *)ptr;
                uint16_t frag_off = ntohs(fh->frag_offset_flags) >> 3;
                uint8_t m_flag = ntohs(fh->frag_offset_flags) & 0x1;
                printf("  Fragment Offset: %u\n", frag_off);
                printf("  More Fragments: %s\n", m_flag ? "Yes" : "No");
                printf("  Identification: 0x%08x\n", ntohl(fh->identification));
                next_hdr = fh->next_header;
                ptr += 8;  // Fragment header is always 8 bytes
                break;
            }
            
            case IPPROTO_DSTOPTS: {
                printf("- Destination Options Header\n");
                const ipv6_ext_hdr_t *doh = (const ipv6_ext_hdr_t *)ptr;
                size_t hdr_size = (doh->hdr_len + 1) * 8;
                printf("  Header Length: %zu bytes\n", hdr_size);
                next_hdr = doh->next_header;
                ptr += hdr_size;
                break;
            }
            
            case IPPROTO_TCP:
                printf("- TCP (Upper Layer Protocol)\n");
                return 0;
                
            case IPPROTO_UDP:
                printf("- UDP (Upper Layer Protocol)\n");
                return 0;
                
            case IPPROTO_ICMPV6:
                printf("- ICMPv6 (Upper Layer Protocol)\n");
                return 0;
                
            default:
                printf("- Unknown/Unsupported Header Type: %d\n", next_hdr);
                return -1;
        }
    }
    
    return 0;
}

// Security validation function
int validate_extension_headers(const uint8_t *packet, size_t packet_len) {
    const struct ip6_hdr *ip6 = (const struct ip6_hdr *)packet;
    const uint8_t *ptr = packet + sizeof(struct ip6_hdr);
    uint8_t next_hdr = ip6->ip6_nxt;
    int ext_hdr_count = 0;
    const int MAX_EXT_HEADERS = 10;  // Security limit
    
    while (ptr < packet + packet_len && ext_hdr_count < MAX_EXT_HEADERS) {
        // Check for deprecated or dangerous headers
        if (next_hdr == IPPROTO_ROUTING) {
            const ipv6_routing_hdr_t *rh = (const ipv6_routing_hdr_t *)ptr;
            if (rh->routing_type == 0) {
                printf("SECURITY: Blocked Type 0 Routing Header\n");
                return -1;
            }
        }
        
        // Check if this is an extension header
        if (next_hdr == IPPROTO_HOPOPTS || next_hdr == IPPROTO_ROUTING ||
            next_hdr == IPPROTO_FRAGMENT || next_hdr == IPPROTO_DSTOPTS) {
            const ipv6_ext_hdr_t *ext = (const ipv6_ext_hdr_t *)ptr;
            size_t hdr_size = (ext->hdr_len + 1) * 8;
            
            // Bounds check
            if (ptr + hdr_size > packet + packet_len) {
                printf("SECURITY: Malformed extension header\n");
                return -1;
            }
            
            next_hdr = ext->next_header;
            ptr += hdr_size;
            ext_hdr_count++;
        } else {
            // Reached upper layer protocol
            break;
        }
    }
    
    if (ext_hdr_count >= MAX_EXT_HEADERS) {
        printf("SECURITY: Too many extension headers\n");
        return -1;
    }
    
    return 0;
}

int main() {
    printf("=== IPv6 Extension Headers Demo ===\n\n");
    
    // Example 1: Create a Hop-by-Hop header
    printf("Example 1: Creating Hop-by-Hop Header with Router Alert\n");
    uint8_t hbh_buffer[8];
    create_hopbyhop_router_alert(hbh_buffer, IPPROTO_TCP, 0);  // MLD alert
    
    printf("Created Hop-by-Hop header:\n");
    for (int i = 0; i < 8; i++) {
        printf("%02x ", hbh_buffer[i]);
    }
    printf("\n\n");
    
    // Example 2: Simulated packet with extension headers
    printf("Example 2: Parsing Extension Headers\n");
    uint8_t simulated_packet[128];
    struct ip6_hdr *sim_ip6 = (struct ip6_hdr *)simulated_packet;
    
    // Create IPv6 header
    sim_ip6->ip6_vfc = 0x60;  // Version 6
    sim_ip6->ip6_nxt = IPPROTO_HOPOPTS;
    inet_pton(AF_INET6, "2001:db8::1", &sim_ip6->ip6_src);
    inet_pton(AF_INET6, "2001:db8::2", &sim_ip6->ip6_dst);
    
    // Add Hop-by-Hop header
    memcpy(simulated_packet + sizeof(struct ip6_hdr), hbh_buffer, 8);
    
    parse_extension_headers(simulated_packet, 128);
    
    printf("\n");
    printf("Example 3: Security Validation\n");
    if (validate_extension_headers(simulated_packet, 128) == 0) {
        printf("Packet passed security validation\n");
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::net::Ipv6Addr;
use std::fmt;

// Extension Header Types
const IPPROTO_HOPOPTS: u8 = 0;
const IPPROTO_ROUTING: u8 = 43;
const IPPROTO_FRAGMENT: u8 = 44;
const IPPROTO_DSTOPTS: u8 = 60;
const IPPROTO_TCP: u8 = 6;
const IPPROTO_UDP: u8 = 17;
const IPPROTO_ICMPV6: u8 = 58;

// Generic Extension Header
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct ExtensionHeader {
    next_header: u8,
    hdr_len: u8,  // Length in 8-byte units, minus 1
}

impl ExtensionHeader {
    fn total_length(&self) -> usize {
        (self.hdr_len as usize + 1) * 8
    }
}

// Hop-by-Hop Options Header
#[repr(C, packed)]
struct HopByHopHeader {
    next_header: u8,
    hdr_len: u8,
    options: [u8; 6],  // Variable length in practice
}

// Router Alert Option
#[repr(C, packed)]
struct RouterAlertOption {
    option_type: u8,   // 5
    length: u8,        // 2
    value: u16,        // Alert value
}

// Routing Header
#[repr(C, packed)]
#[derive(Debug)]
struct RoutingHeader {
    next_header: u8,
    hdr_len: u8,
    routing_type: u8,
    segments_left: u8,
}

// Fragment Header
#[repr(C, packed)]
#[derive(Debug)]
struct FragmentHeader {
    next_header: u8,
    reserved: u8,
    frag_offset_flags: u16,
    identification: u32,
}

impl FragmentHeader {
    fn fragment_offset(&self) -> u16 {
        u16::from_be(self.frag_offset_flags) >> 3
    }
    
    fn more_fragments(&self) -> bool {
        (u16::from_be(self.frag_offset_flags) & 0x1) != 0
    }
}

// IPv6 Packet Parser
struct Ipv6PacketParser<'a> {
    data: &'a [u8],
    position: usize,
}

#[derive(Debug)]
enum ExtensionHeaderType {
    HopByHop,
    Routing { routing_type: u8, segments_left: u8 },
    Fragment { offset: u16, more_fragments: bool, id: u32 },
    DestinationOptions,
    UpperLayer(u8),
    Unknown(u8),
}

impl<'a> Ipv6PacketParser<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, position: 40 } // Skip IPv6 base header
    }
    
    fn parse_extension_headers(&mut self, mut next_header: u8) 
        -> Result<Vec<ExtensionHeaderType>, &'static str> {
        let mut headers = Vec::new();
        const MAX_HEADERS: usize = 10;
        
        while self.position < self.data.len() && headers.len() < MAX_HEADERS {
            match next_header {
                IPPROTO_HOPOPTS => {
                    if self.position + 2 > self.data.len() {
                        return Err("Truncated Hop-by-Hop header");
                    }
                    
                    let ext_hdr = self.read_extension_header()?;
                    next_header = ext_hdr.0;
                    headers.push(ExtensionHeaderType::HopByHop);
                }
                
                IPPROTO_ROUTING => {
                    if self.position + 4 > self.data.len() {
                        return Err("Truncated Routing header");
                    }
                    
                    let routing_type = self.data[self.position + 2];
                    let segments_left = self.data[self.position + 3];
                    
                    // Security check for deprecated Type 0
                    if routing_type == 0 {
                        return Err("Deprecated Type 0 Routing Header detected");
                    }
                    
                    let ext_hdr = self.read_extension_header()?;
                    next_header = ext_hdr.0;
                    
                    headers.push(ExtensionHeaderType::Routing {
                        routing_type,
                        segments_left,
                    });
                }
                
                IPPROTO_FRAGMENT => {
                    if self.position + 8 > self.data.len() {
                        return Err("Truncated Fragment header");
                    }
                    
                    let frag_next = self.data[self.position];
                    let frag_offset_flags = u16::from_be_bytes([
                        self.data[self.position + 2],
                        self.data[self.position + 3],
                    ]);
                    let identification = u32::from_be_bytes([
                        self.data[self.position + 4],
                        self.data[self.position + 5],
                        self.data[self.position + 6],
                        self.data[self.position + 7],
                    ]);
                    
                    let offset = frag_offset_flags >> 3;
                    let more_fragments = (frag_offset_flags & 0x1) != 0;
                    
                    next_header = frag_next;
                    self.position += 8;
                    
                    headers.push(ExtensionHeaderType::Fragment {
                        offset,
                        more_fragments,
                        id: identification,
                    });
                }
                
                IPPROTO_DSTOPTS => {
                    let ext_hdr = self.read_extension_header()?;
                    next_header = ext_hdr.0;
                    headers.push(ExtensionHeaderType::DestinationOptions);
                }
                
                IPPROTO_TCP | IPPROTO_UDP | IPPROTO_ICMPV6 => {
                    headers.push(ExtensionHeaderType::UpperLayer(next_header));
                    break;
                }
                
                _ => {
                    headers.push(ExtensionHeaderType::Unknown(next_header));
                    break;
                }
            }
        }
        
        if headers.len() >= MAX_HEADERS {
            return Err("Too many extension headers (DoS protection)");
        }
        
        Ok(headers)
    }
    
    fn read_extension_header(&mut self) -> Result<(u8, usize), &'static str> {
        if self.position + 2 > self.data.len() {
            return Err("Buffer overflow");
        }
        
        let next_header = self.data[self.position];
        let hdr_len = self.data[self.position + 1];
        let total_len = (hdr_len as usize + 1) * 8;
        
        if self.position + total_len > self.data.len() {
            return Err("Malformed extension header");
        }
        
        self.position += total_len;
        Ok((next_header, total_len))
    }
}

// Hop-by-Hop Header Builder
struct HopByHopBuilder {
    next_header: u8,
    options: Vec<u8>,
}

impl HopByHopBuilder {
    fn new(next_header: u8) -> Self {
        Self {
            next_header,
            options: Vec::new(),
        }
    }
    
    fn add_router_alert(&mut self, value: u16) {
        self.options.push(5);  // Router Alert type
        self.options.push(2);  // Length
        self.options.extend_from_slice(&value.to_be_bytes());
    }
    
    fn add_padding(&mut self, size: usize) {
        if size == 1 {
            self.options.push(0);  // Pad1
        } else if size > 1 {
            self.options.push(1);  // PadN
            self.options.push((size - 2) as u8);
            self.options.extend(vec![0; size - 2]);
        }
    }
    
    fn build(&mut self) -> Vec<u8> {
        // Ensure 8-byte alignment
        let current_len = 2 + self.options.len();
        let padding_needed = (8 - (current_len % 8)) % 8;
        self.add_padding(padding_needed);
        
        let total_len = 2 + self.options.len();
        let hdr_len = (total_len / 8) - 1;
        
        let mut result = vec![self.next_header, hdr_len as u8];
        result.extend_from_slice(&self.options);
        result
    }
}

// Security Validator
struct SecurityValidator {
    max_extension_headers: usize,
    allow_type0_routing: bool,
    max_fragment_size: u16,
}

impl SecurityValidator {
    fn new() -> Self {
        Self {
            max_extension_headers: 10,
            allow_type0_routing: false,
            max_fragment_size: 1280,
        }
    }
    
    fn validate(&self, headers: &[ExtensionHeaderType]) -> Result<(), String> {
        if headers.len() > self.max_extension_headers {
            return Err(format!(
                "Too many extension headers: {} (max: {})",
                headers.len(),
                self.max_extension_headers
            ));
        }
        
        for header in headers {
            match header {
                ExtensionHeaderType::Routing { routing_type, .. } => {
                    if *routing_type == 0 && !self.allow_type0_routing {
                        return Err("Type 0 Routing Header is deprecated and blocked".to_string());
                    }
                }
                ExtensionHeaderType::Fragment { offset, .. } => {
                    if *offset > self.max_fragment_size {
                        return Err(format!(
                            "Fragment offset too large: {} (max: {})",
                            offset, self.max_fragment_size
                        ));
                    }
                }
                _ => {}
            }
        }
        
        Ok(())
    }
}

fn main() {
    println!("=== IPv6 Extension Headers in Rust ===\n");
    
    // Example 1: Build Hop-by-Hop header with Router Alert
    println!("Example 1: Building Hop-by-Hop Header");
    let mut hbh_builder = HopByHopBuilder::new(IPPROTO_TCP);
    hbh_builder.add_router_alert(0);  // MLD alert
    let hbh_bytes = hbh_builder.build();
    
    println!("Hop-by-Hop header ({} bytes):", hbh_bytes.len());
    for (i, byte) in hbh_bytes.iter().enumerate() {
        print!("{:02x} ", byte);
        if (i + 1) % 8 == 0 {
            println!();
        }
    }
    println!("\n");
    
    // Example 2: Parse extension headers from simulated packet
    println!("Example 2: Parsing Extension Headers");
    
    // Simulate IPv6 packet with extension headers
    let mut packet = vec![0u8; 200];
    
    // IPv6 base header (40 bytes)
    packet[0] = 0x60;  // Version 6
    packet[6] = IPPROTO_HOPOPTS;  // Next header
    
    // Hop-by-Hop header at offset 40
    packet[40..40 + hbh_bytes.len()].copy_from_slice(&hbh_bytes);
    
    let mut parser = Ipv6PacketParser::new(&packet);
    match parser.parse_extension_headers(IPPROTO_HOPOPTS) {
        Ok(headers) => {
            println!("Found {} extension header(s):", headers.len());
            for (i, header) in headers.iter().enumerate() {
                println!("  {}. {:?}", i + 1, header);
            }
        }
        Err(e) => {
            println!("Error parsing headers: {}", e);
        }
    }
    
    println!();
    
    // Example 3: Security validation
    println!("Example 3: Security Validation");
    let validator = SecurityValidator::new();
    
    let test_headers = vec![
        ExtensionHeaderType::HopByHop,
        ExtensionHeaderType::Routing {
            routing_type: 2,
            segments_left: 0,
        },
        ExtensionHeaderType::Fragment {
            offset: 100,
            more_fragments: false,
            id: 12345,
        },
        ExtensionHeaderType::UpperLayer(IPPROTO_TCP),
    ];
    
    match validator.validate(&test_headers) {
        Ok(()) => println!("✓ Packet passed security validation"),
        Err(e) => println!("✗ Security validation failed: {}", e),
    }
    
    // Test with deprecated Type 0 routing header
    let malicious_headers = vec![
        ExtensionHeaderType::Routing {
            routing_type: 0,  // Deprecated!
            segments_left: 5,
        },
    ];
    
    match validator.validate(&malicious_headers) {
        Ok(()) => println!("✓ Malicious packet passed (should not happen)"),
        Err(e) => println!("✓ Correctly blocked: {}", e),
    }
}
```

---

## Summary

**IPv6 Extension Headers** provide a modular, extensible mechanism for adding functionality to IPv6 without modifying the base protocol. Key points:

**Architecture:**
- Chain of headers between base IPv6 header and payload
- Each header contains "Next Header" field
- Defined processing order for efficiency

**Main Extension Headers:**
1. **Hop-by-Hop Options** - Processed by every router (Router Alert, Jumbo Payload)
2. **Routing Header** - Specifies intermediate nodes (Types 2, 3, 4 used; Type 0 deprecated)
3. **Fragment Header** - Handles packet fragmentation
4. **Destination Options** - Processed only by destination

**Critical Security Concerns:**
- **Type 0 Routing Header** - Deprecated due to amplification attacks and firewall bypass
- **Processing Overhead** - Complex headers can overwhelm routers (DoS vector)
- **Fragmentation Exploits** - Overlapping fragments, resource exhaustion
- **Firewall Evasion** - Headers can obscure payload for inspection
- **Resource Attacks** - Excessive or malformed header chains

**Best Practices:**
- Implement strict header validation and limits
- Drop deprecated routing headers (Type 0)
- Rate-limit packets with complex extension headers
- Use stateful inspection for fragments
- Limit maximum extension header chain length
- Monitor for anomalous extension header usage

Extension headers demonstrate IPv6's flexibility but require careful implementation to prevent security vulnerabilities. Modern networks should enforce strict policies around extension header processing, particularly for deprecated or potentially dangerous header types.