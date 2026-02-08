# IP Fragmentation and Reassembly

## Detailed Description

### Overview
IP fragmentation is a process where IP datagrams that exceed the Maximum Transmission Unit (MTU) of a network link are broken into smaller fragments that can be transmitted across that link. The destination host is then responsible for reassembling these fragments back into the original datagram.

### Why Fragmentation is Needed
- Different network technologies have different MTU sizes (Ethernet: 1500 bytes, PPPoE: 1492 bytes, etc.)
- When a packet is too large for the next hop's MTU, it must be fragmented
- Fragmentation can occur at the source or at intermediate routers (IPv4 only)
- IPv6 requires path MTU discovery and doesn't allow intermediate fragmentation

### Fragmentation Mechanics

#### IPv4 Header Fields
The IPv4 header contains three key fields for fragmentation:
- **Identification (16 bits)**: Unique ID for all fragments of the original datagram
- **Flags (3 bits)**:
  - Bit 0: Reserved (must be 0)
  - Bit 1: Don't Fragment (DF) - prevents fragmentation
  - Bit 2: More Fragments (MF) - set to 1 for all fragments except the last
- **Fragment Offset (13 bits)**: Position of fragment in original datagram (in 8-byte units)

#### Fragmentation Process
1. Original packet exceeds MTU
2. Router/host creates multiple fragments
3. Each fragment gets:
   - Same Identification value
   - Appropriate Fragment Offset
   - MF flag set (except last fragment)
   - Recalculated header checksum
   - Total Length adjusted to fragment size

### Reassembly Process

The destination host performs reassembly:
1. **Buffer Management**: Allocates reassembly buffer based on first fragment
2. **Fragment Collection**: Collects all fragments with matching (Source IP, Destination IP, Protocol, Identification)
3. **Ordering**: Uses Fragment Offset to place fragments correctly
4. **Completion Detection**: Last fragment (MF=0) indicates total size
5. **Timeout**: If all fragments don't arrive within timeout (typically 60-120 seconds), discard all fragments

### Fragment Attacks

#### Types of Fragment-Based Attacks

1. **Fragment Overlap Attacks**
   - Overlapping fragment offsets with different data
   - Can bypass intrusion detection systems
   - Can corrupt reassembled packet

2. **Tiny Fragment Attack**
   - First fragment too small to contain complete transport header
   - Bypasses packet filters that only examine first fragment

3. **Fragment Flood (DoS)**
   - Send many incomplete fragment sets
   - Exhaust reassembly buffer resources
   - Cause memory exhaustion

4. **Teardrop Attack**
   - Overlapping fragments with negative offsets
   - Exploits bugs in reassembly code
   - Can crash vulnerable systems

5. **Rose Attack**
   - Send fragments out of order
   - Exploit inefficient reassembly algorithms
   - Cause CPU exhaustion

## Programming Examples

### C/C++ Examples

#### 1. Examining Fragment Fields

```c
#include <stdio.h>
#include <string.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

// Structure to parse IP header
void parse_ip_header(unsigned char *packet, int len) {
    struct iphdr *ip = (struct iphdr *)packet;
    
    // Extract fragment fields
    uint16_t frag_offset = ntohs(ip->frag_off);
    int df = (frag_offset & IP_DF) != 0;  // Don't Fragment
    int mf = (frag_offset & IP_MF) != 0;  // More Fragments
    uint16_t offset = (frag_offset & IP_OFFMASK) * 8;  // Offset in bytes
    
    printf("IP Header Information:\n");
    printf("  Version: %d\n", ip->version);
    printf("  Header Length: %d bytes\n", ip->ihl * 4);
    printf("  Total Length: %d bytes\n", ntohs(ip->tot_len));
    printf("  Identification: 0x%04x (%d)\n", ntohs(ip->id), ntohs(ip->id));
    printf("  Flags: DF=%d MF=%d\n", df, mf);
    printf("  Fragment Offset: %d bytes\n", offset);
    printf("  Protocol: %d\n", ip->protocol);
    
    if (mf || offset > 0) {
        printf("  ** This is a fragment **\n");
        if (mf) {
            printf("  ** More fragments expected **\n");
        } else {
            printf("  ** Last fragment **\n");
        }
    }
}
```

#### 2. Simple Reassembly Buffer Structure

```c
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define MAX_FRAGMENTS 100
#define REASSEMBLY_TIMEOUT 60  // seconds

typedef struct {
    uint8_t *data;
    uint16_t offset;
    uint16_t length;
} Fragment;

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t protocol;
    uint16_t identification;
    
    Fragment fragments[MAX_FRAGMENTS];
    int fragment_count;
    
    uint16_t total_length;
    int last_fragment_received;
    time_t start_time;
    
    uint8_t *reassembled_data;
} ReassemblyBuffer;

// Initialize reassembly buffer
ReassemblyBuffer* create_reassembly_buffer(uint32_t src_ip, uint32_t dst_ip,
                                           uint8_t protocol, uint16_t id) {
    ReassemblyBuffer *buf = (ReassemblyBuffer*)calloc(1, sizeof(ReassemblyBuffer));
    if (!buf) return NULL;
    
    buf->src_ip = src_ip;
    buf->dst_ip = dst_ip;
    buf->protocol = protocol;
    buf->identification = id;
    buf->start_time = time(NULL);
    buf->last_fragment_received = 0;
    
    return buf;
}

// Add fragment to reassembly buffer
int add_fragment(ReassemblyBuffer *buf, uint8_t *data, uint16_t offset,
                 uint16_t length, int more_fragments) {
    if (buf->fragment_count >= MAX_FRAGMENTS) {
        return -1;  // Too many fragments
    }
    
    // Check for timeout
    if (time(NULL) - buf->start_time > REASSEMBLY_TIMEOUT) {
        return -2;  // Timeout
    }
    
    // Store fragment
    Fragment *frag = &buf->fragments[buf->fragment_count];
    frag->data = (uint8_t*)malloc(length);
    if (!frag->data) return -1;
    
    memcpy(frag->data, data, length);
    frag->offset = offset;
    frag->length = length;
    buf->fragment_count++;
    
    // If this is the last fragment, record total length
    if (!more_fragments) {
        buf->total_length = offset + length;
        buf->last_fragment_received = 1;
    }
    
    return 0;
}

// Check if reassembly is complete
int is_reassembly_complete(ReassemblyBuffer *buf) {
    if (!buf->last_fragment_received) {
        return 0;  // Haven't received last fragment yet
    }
    
    // Check if we have all bytes covered
    uint8_t *coverage = (uint8_t*)calloc(buf->total_length, 1);
    if (!coverage) return 0;
    
    for (int i = 0; i < buf->fragment_count; i++) {
        Fragment *frag = &buf->fragments[i];
        for (int j = 0; j < frag->length; j++) {
            if (frag->offset + j < buf->total_length) {
                coverage[frag->offset + j] = 1;
            }
        }
    }
    
    // Check for gaps
    int complete = 1;
    for (int i = 0; i < buf->total_length; i++) {
        if (!coverage[i]) {
            complete = 0;
            break;
        }
    }
    
    free(coverage);
    return complete;
}

// Perform reassembly
uint8_t* reassemble_packet(ReassemblyBuffer *buf) {
    if (!is_reassembly_complete(buf)) {
        return NULL;
    }
    
    buf->reassembled_data = (uint8_t*)calloc(buf->total_length, 1);
    if (!buf->reassembled_data) return NULL;
    
    // Copy fragments into reassembled buffer
    for (int i = 0; i < buf->fragment_count; i++) {
        Fragment *frag = &buf->fragments[i];
        memcpy(buf->reassembled_data + frag->offset, frag->data, frag->length);
    }
    
    return buf->reassembled_data;
}

// Clean up
void free_reassembly_buffer(ReassemblyBuffer *buf) {
    if (!buf) return;
    
    for (int i = 0; i < buf->fragment_count; i++) {
        free(buf->fragments[i].data);
    }
    
    free(buf->reassembled_data);
    free(buf);
}
```

#### 3. Fragment Creation (Simplified)

```c
#include <netinet/ip.h>
#include <string.h>

// Fragment a packet
int fragment_packet(uint8_t *original_packet, int packet_len, int mtu,
                   uint8_t ***fragments_out, int **fragment_lens_out,
                   int *num_fragments_out) {
    struct iphdr *ip = (struct iphdr *)original_packet;
    int header_len = ip->ihl * 4;
    int data_len = ntohs(ip->tot_len) - header_len;
    
    // Calculate fragment size (must be multiple of 8)
    int max_frag_data = ((mtu - header_len) / 8) * 8;
    int num_fragments = (data_len + max_frag_data - 1) / max_frag_data;
    
    // Allocate arrays
    uint8_t **fragments = (uint8_t**)malloc(num_fragments * sizeof(uint8_t*));
    int *fragment_lens = (int*)malloc(num_fragments * sizeof(int));
    
    uint8_t *payload = original_packet + header_len;
    int offset = 0;
    
    for (int i = 0; i < num_fragments; i++) {
        int frag_data_len = (i == num_fragments - 1) ? 
                           (data_len - offset) : max_frag_data;
        int frag_len = header_len + frag_data_len;
        
        // Allocate fragment
        fragments[i] = (uint8_t*)malloc(frag_len);
        fragment_lens[i] = frag_len;
        
        // Copy IP header
        memcpy(fragments[i], original_packet, header_len);
        
        // Update fragment fields
        struct iphdr *frag_ip = (struct iphdr *)fragments[i];
        frag_ip->tot_len = htons(frag_len);
        
        uint16_t frag_off = (offset / 8);
        if (i < num_fragments - 1) {
            frag_off |= IP_MF;  // More Fragments flag
        }
        frag_ip->frag_off = htons(frag_off);
        
        // Recalculate checksum
        frag_ip->check = 0;
        frag_ip->check = ip_checksum((uint16_t*)frag_ip, header_len);
        
        // Copy fragment data
        memcpy(fragments[i] + header_len, payload + offset, frag_data_len);
        
        offset += frag_data_len;
    }
    
    *fragments_out = fragments;
    *fragment_lens_out = fragment_lens;
    *num_fragments_out = num_fragments;
    
    return 0;
}

// Simple IP checksum calculation
uint16_t ip_checksum(uint16_t *buf, int len) {
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *(uint8_t*)buf;
    }
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    
    return ~sum;
}
```

### Rust Examples

#### 1. Fragment Parser

```rust
use std::net::Ipv4Addr;

#[derive(Debug, Clone)]
pub struct IpFragment {
    pub src_addr: Ipv4Addr,
    pub dst_addr: Ipv4Addr,
    pub protocol: u8,
    pub identification: u16,
    pub offset: u16,
    pub more_fragments: bool,
    pub data: Vec<u8>,
}

#[derive(Debug)]
pub struct IpHeader {
    pub version: u8,
    pub ihl: u8,
    pub total_length: u16,
    pub identification: u16,
    pub flags: u8,
    pub fragment_offset: u16,
    pub ttl: u8,
    pub protocol: u8,
    pub checksum: u16,
    pub src_addr: Ipv4Addr,
    pub dst_addr: Ipv4Addr,
}

impl IpHeader {
    /// Parse IP header from raw bytes
    pub fn parse(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < 20 {
            return Err("Insufficient data for IP header");
        }
        
        let version = data[0] >> 4;
        let ihl = data[0] & 0x0F;
        let total_length = u16::from_be_bytes([data[2], data[3]]);
        let identification = u16::from_be_bytes([data[4], data[5]]);
        
        let flags_and_offset = u16::from_be_bytes([data[6], data[7]]);
        let flags = (flags_and_offset >> 13) as u8;
        let fragment_offset = flags_and_offset & 0x1FFF;
        
        let ttl = data[8];
        let protocol = data[9];
        let checksum = u16::from_be_bytes([data[10], data[11]]);
        
        let src_addr = Ipv4Addr::new(data[12], data[13], data[14], data[15]);
        let dst_addr = Ipv4Addr::new(data[16], data[17], data[18], data[19]);
        
        Ok(IpHeader {
            version,
            ihl,
            total_length,
            identification,
            flags,
            fragment_offset,
            ttl,
            protocol,
            checksum,
            src_addr,
            dst_addr,
        })
    }
    
    /// Check if Don't Fragment flag is set
    pub fn dont_fragment(&self) -> bool {
        (self.flags & 0x02) != 0
    }
    
    /// Check if More Fragments flag is set
    pub fn more_fragments(&self) -> bool {
        (self.flags & 0x01) != 0
    }
    
    /// Get fragment offset in bytes
    pub fn offset_bytes(&self) -> u16 {
        self.fragment_offset * 8
    }
    
    /// Check if this is a fragment
    pub fn is_fragment(&self) -> bool {
        self.more_fragments() || self.fragment_offset != 0
    }
}

/// Parse packet into IpFragment
pub fn parse_fragment(packet: &[u8]) -> Result<IpFragment, &'static str> {
    let header = IpHeader::parse(packet)?;
    
    let header_len = (header.ihl as usize) * 4;
    if packet.len() < header_len {
        return Err("Packet smaller than header length");
    }
    
    let data = packet[header_len..].to_vec();
    
    Ok(IpFragment {
        src_addr: header.src_addr,
        dst_addr: header.dst_addr,
        protocol: header.protocol,
        identification: header.identification,
        offset: header.offset_bytes(),
        more_fragments: header.more_fragments(),
        data,
    })
}
```

#### 2. Reassembly Engine

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
struct FragmentKey {
    src_addr: Ipv4Addr,
    dst_addr: Ipv4Addr,
    protocol: u8,
    identification: u16,
}

struct ReassemblyBuffer {
    fragments: Vec<IpFragment>,
    total_length: Option<u16>,
    start_time: Instant,
}

pub struct ReassemblyEngine {
    buffers: HashMap<FragmentKey, ReassemblyBuffer>,
    timeout: Duration,
}

impl ReassemblyEngine {
    pub fn new(timeout_secs: u64) -> Self {
        ReassemblyEngine {
            buffers: HashMap::new(),
            timeout: Duration::from_secs(timeout_secs),
        }
    }
    
    /// Add a fragment to the reassembly engine
    pub fn add_fragment(&mut self, fragment: IpFragment) -> Option<Vec<u8>> {
        // Clean up old buffers first
        self.cleanup_expired();
        
        let key = FragmentKey {
            src_addr: fragment.src_addr,
            dst_addr: fragment.dst_addr,
            protocol: fragment.protocol,
            identification: fragment.identification,
        };
        
        let buffer = self.buffers.entry(key.clone()).or_insert_with(|| {
            ReassemblyBuffer {
                fragments: Vec::new(),
                total_length: None,
                start_time: Instant::now(),
            }
        });
        
        // If this is the last fragment, record total length
        if !fragment.more_fragments {
            buffer.total_length = Some(fragment.offset + fragment.data.len() as u16);
        }
        
        buffer.fragments.push(fragment);
        
        // Check if reassembly is complete
        if self.is_complete(&buffer) {
            let reassembled = self.reassemble(&buffer);
            self.buffers.remove(&key);
            return Some(reassembled);
        }
        
        None
    }
    
    /// Check if all fragments have been received
    fn is_complete(&self, buffer: &ReassemblyBuffer) -> bool {
        let total_length = match buffer.total_length {
            Some(len) => len,
            None => return false, // Haven't received last fragment
        };
        
        // Create coverage map
        let mut coverage = vec![false; total_length as usize];
        
        for frag in &buffer.fragments {
            let start = frag.offset as usize;
            let end = start + frag.data.len();
            
            if end <= coverage.len() {
                for i in start..end {
                    coverage[i] = true;
                }
            }
        }
        
        // Check for gaps
        coverage.iter().all(|&covered| covered)
    }
    
    /// Reassemble fragments into complete packet
    fn reassemble(&self, buffer: &ReassemblyBuffer) -> Vec<u8> {
        let total_length = buffer.total_length.unwrap() as usize;
        let mut result = vec![0u8; total_length];
        
        for frag in &buffer.fragments {
            let start = frag.offset as usize;
            let end = start + frag.data.len();
            
            if end <= result.len() {
                result[start..end].copy_from_slice(&frag.data);
            }
        }
        
        result
    }
    
    /// Remove expired reassembly buffers
    fn cleanup_expired(&mut self) {
        let now = Instant::now();
        self.buffers.retain(|_, buffer| {
            now.duration_since(buffer.start_time) < self.timeout
        });
    }
    
    /// Get statistics
    pub fn stats(&self) -> (usize, usize) {
        let buffer_count = self.buffers.len();
        let fragment_count: usize = self.buffers.values()
            .map(|b| b.fragments.len())
            .sum();
        (buffer_count, fragment_count)
    }
}
```

#### 3. Fragment Attack Detection

```rust
/// Detect potential fragment-based attacks
pub struct FragmentAnalyzer {
    min_first_fragment_size: usize,
    max_fragments_per_packet: usize,
}

#[derive(Debug)]
pub enum FragmentAnomaly {
    TinyFirstFragment,
    TooManyFragments,
    OverlappingFragments,
    InvalidOffset,
}

impl FragmentAnalyzer {
    pub fn new() -> Self {
        FragmentAnalyzer {
            min_first_fragment_size: 68, // Minimum to contain TCP/UDP header
            max_fragments_per_packet: 100,
        }
    }
    
    /// Analyze fragments for potential attacks
    pub fn analyze(&self, fragments: &[IpFragment]) -> Vec<FragmentAnomaly> {
        let mut anomalies = Vec::new();
        
        // Check for tiny first fragment attack
        if let Some(first) = fragments.iter().find(|f| f.offset == 0) {
            if first.data.len() < self.min_first_fragment_size {
                anomalies.push(FragmentAnomaly::TinyFirstFragment);
            }
        }
        
        // Check for too many fragments
        if fragments.len() > self.max_fragments_per_packet {
            anomalies.push(FragmentAnomaly::TooManyFragments);
        }
        
        // Check for overlapping fragments
        for i in 0..fragments.len() {
            for j in (i + 1)..fragments.len() {
                let frag1 = &fragments[i];
                let frag2 = &fragments[j];
                
                let end1 = frag1.offset + frag1.data.len() as u16;
                let end2 = frag2.offset + frag2.data.len() as u16;
                
                // Check if fragments overlap
                if (frag1.offset < end2 && frag2.offset < end1) &&
                   (frag1.offset != frag2.offset || frag1.data.len() != frag2.data.len()) {
                    anomalies.push(FragmentAnomaly::OverlappingFragments);
                    break;
                }
            }
        }
        
        anomalies
    }
}

/// Example usage
pub fn example_fragment_detection() {
    let analyzer = FragmentAnalyzer::new();
    
    let fragments = vec![
        IpFragment {
            src_addr: Ipv4Addr::new(192, 168, 1, 1),
            dst_addr: Ipv4Addr::new(192, 168, 1, 2),
            protocol: 6,
            identification: 12345,
            offset: 0,
            more_fragments: true,
            data: vec![0u8; 20], // Suspiciously small
        },
    ];
    
    let anomalies = analyzer.analyze(&fragments);
    
    for anomaly in anomalies {
        println!("Detected anomaly: {:?}", anomaly);
    }
}
```

#### 4. Complete Example with Testing

```rust
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_simple_reassembly() {
        let mut engine = ReassemblyEngine::new(60);
        
        // Create three fragments
        let frag1 = IpFragment {
            src_addr: Ipv4Addr::new(10, 0, 0, 1),
            dst_addr: Ipv4Addr::new(10, 0, 0, 2),
            protocol: 6,
            identification: 100,
            offset: 0,
            more_fragments: true,
            data: b"Hello ".to_vec(),
        };
        
        let frag2 = IpFragment {
            src_addr: Ipv4Addr::new(10, 0, 0, 1),
            dst_addr: Ipv4Addr::new(10, 0, 0, 2),
            protocol: 6,
            identification: 100,
            offset: 6,
            more_fragments: true,
            data: b"World".to_vec(),
        };
        
        let frag3 = IpFragment {
            src_addr: Ipv4Addr::new(10, 0, 0, 1),
            dst_addr: Ipv4Addr::new(10, 0, 0, 2),
            protocol: 6,
            identification: 100,
            offset: 11,
            more_fragments: false,
            data: b"!".to_vec(),
        };
        
        // Add fragments
        assert!(engine.add_fragment(frag1).is_none());
        assert!(engine.add_fragment(frag2).is_none());
        
        let result = engine.add_fragment(frag3);
        assert!(result.is_some());
        
        let reassembled = result.unwrap();
        assert_eq!(reassembled, b"Hello World!");
    }
    
    #[test]
    fn test_fragment_anomaly_detection() {
        let analyzer = FragmentAnalyzer::new();
        
        // Tiny first fragment
        let tiny_frag = IpFragment {
            src_addr: Ipv4Addr::new(10, 0, 0, 1),
            dst_addr: Ipv4Addr::new(10, 0, 0, 2),
            protocol: 6,
            identification: 200,
            offset: 0,
            more_fragments: true,
            data: vec![0u8; 10], // Too small
        };
        
        let anomalies = analyzer.analyze(&[tiny_frag]);
        assert!(!anomalies.is_empty());
    }
}
```

## Summary

**IP Fragmentation and Reassembly** is a critical mechanism in IP networking that enables transmission of packets larger than the network's MTU. Key points:

### Core Concepts:
- **Fragmentation** occurs when packets exceed MTU; fragments share an Identification value and use Fragment Offset and More Fragments flag
- **Reassembly** happens only at the destination using a timeout-based buffer system
- **IPv6** eliminates intermediate fragmentation, requiring path MTU discovery

### Implementation Challenges:
- Managing reassembly buffers and timeouts
- Handling out-of-order fragment arrival
- Detecting missing fragments and gaps
- Resource management (memory, CPU)

### Security Concerns:
- **Fragment-based attacks** exploit reassembly weaknesses (overlapping, tiny fragments, floods)
- Modern systems implement strict fragment validation and resource limits
- Firewall/IDS must track fragment state to prevent evasion

### Best Practices:
- Implement path MTU discovery to avoid fragmentation
- Set DF flag when possible
- Validate fragment parameters strictly
- Limit reassembly buffer resources
- Use timeouts to prevent resource exhaustion
- Monitor for anomalous fragment patterns

IP fragmentation is increasingly discouraged in modern networks due to performance overhead and security risks, with path MTU discovery being the preferred approach.