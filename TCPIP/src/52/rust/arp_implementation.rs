/*
 * ARP Protocol Implementation in Rust
 * Demonstrates ARP packet creation, parsing, and cache management
 * Uses pnet for packet manipulation (add to Cargo.toml: pnet = "0.34")
 */

use std::collections::HashMap;
use std::net::Ipv4Addr;
use std::time::{Duration, SystemTime};

// ARP Operation codes
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
pub enum ArpOperation {
    Request = 1,
    Reply = 2,
}

impl ArpOperation {
    fn from_u16(value: u16) -> Option<Self> {
        match value {
            1 => Some(ArpOperation::Request),
            2 => Some(ArpOperation::Reply),
            _ => None,
        }
    }
}

// MAC Address type
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MacAddress([u8; 6]);

impl MacAddress {
    pub fn new(bytes: [u8; 6]) -> Self {
        MacAddress(bytes)
    }
    
    pub fn broadcast() -> Self {
        MacAddress([0xff; 6])
    }
    
    pub fn zero() -> Self {
        MacAddress([0; 6])
    }
    
    pub fn as_bytes(&self) -> &[u8; 6] {
        &self.0
    }
}

impl std::fmt::Display for MacAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
               self.0[0], self.0[1], self.0[2], 
               self.0[3], self.0[4], self.0[5])
    }
}

// ARP Packet structure
#[derive(Debug, Clone)]
pub struct ArpPacket {
    pub hw_type: u16,              // Hardware type (1 = Ethernet)
    pub proto_type: u16,           // Protocol type (0x0800 = IPv4)
    pub hw_addr_len: u8,           // Hardware address length
    pub proto_addr_len: u8,        // Protocol address length
    pub operation: ArpOperation,   // Operation (Request/Reply)
    pub sender_hw_addr: MacAddress,
    pub sender_proto_addr: Ipv4Addr,
    pub target_hw_addr: MacAddress,
    pub target_proto_addr: Ipv4Addr,
}

impl ArpPacket {
    // Create ARP request
    pub fn new_request(
        sender_mac: MacAddress,
        sender_ip: Ipv4Addr,
        target_ip: Ipv4Addr,
    ) -> Self {
        ArpPacket {
            hw_type: 1,
            proto_type: 0x0800,
            hw_addr_len: 6,
            proto_addr_len: 4,
            operation: ArpOperation::Request,
            sender_hw_addr: sender_mac,
            sender_proto_addr: sender_ip,
            target_hw_addr: MacAddress::zero(),
            target_proto_addr: target_ip,
        }
    }
    
    // Create ARP reply
    pub fn new_reply(
        sender_mac: MacAddress,
        sender_ip: Ipv4Addr,
        target_mac: MacAddress,
        target_ip: Ipv4Addr,
    ) -> Self {
        ArpPacket {
            hw_type: 1,
            proto_type: 0x0800,
            hw_addr_len: 6,
            proto_addr_len: 4,
            operation: ArpOperation::Reply,
            sender_hw_addr: sender_mac,
            sender_proto_addr: sender_ip,
            target_hw_addr: target_mac,
            target_proto_addr: target_ip,
        }
    }
    
    // Create Gratuitous ARP
    pub fn new_gratuitous(mac: MacAddress, ip: Ipv4Addr) -> Self {
        ArpPacket {
            hw_type: 1,
            proto_type: 0x0800,
            hw_addr_len: 6,
            proto_addr_len: 4,
            operation: ArpOperation::Reply, // or Request
            sender_hw_addr: mac,
            sender_proto_addr: ip,
            target_hw_addr: MacAddress::zero(),
            target_proto_addr: ip, // Same as sender
        }
    }
    
    // Serialize to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(28);
        
        bytes.extend_from_slice(&self.hw_type.to_be_bytes());
        bytes.extend_from_slice(&self.proto_type.to_be_bytes());
        bytes.push(self.hw_addr_len);
        bytes.push(self.proto_addr_len);
        bytes.extend_from_slice(&(self.operation as u16).to_be_bytes());
        bytes.extend_from_slice(self.sender_hw_addr.as_bytes());
        bytes.extend_from_slice(&self.sender_proto_addr.octets());
        bytes.extend_from_slice(self.target_hw_addr.as_bytes());
        bytes.extend_from_slice(&self.target_proto_addr.octets());
        
        bytes
    }
    
    // Deserialize from bytes
    pub fn from_bytes(data: &[u8]) -> Result<Self, String> {
        if data.len() < 28 {
            return Err("Insufficient data for ARP packet".to_string());
        }
        
        let hw_type = u16::from_be_bytes([data[0], data[1]]);
        let proto_type = u16::from_be_bytes([data[2], data[3]]);
        let hw_addr_len = data[4];
        let proto_addr_len = data[5];
        let operation = u16::from_be_bytes([data[6], data[7]]);
        
        let operation = ArpOperation::from_u16(operation)
            .ok_or("Invalid ARP operation")?;
        
        let sender_hw_addr = MacAddress::new([
            data[8], data[9], data[10], data[11], data[12], data[13]
        ]);
        
        let sender_proto_addr = Ipv4Addr::new(
            data[14], data[15], data[16], data[17]
        );
        
        let target_hw_addr = MacAddress::new([
            data[18], data[19], data[20], data[21], data[22], data[23]
        ]);
        
        let target_proto_addr = Ipv4Addr::new(
            data[24], data[25], data[26], data[27]
        );
        
        Ok(ArpPacket {
            hw_type,
            proto_type,
            hw_addr_len,
            proto_addr_len,
            operation,
            sender_hw_addr,
            sender_proto_addr,
            target_hw_addr,
            target_proto_addr,
        })
    }
    
    // Check if this is a Gratuitous ARP
    pub fn is_gratuitous(&self) -> bool {
        self.sender_proto_addr == self.target_proto_addr
    }
}

impl std::fmt::Display for ArpPacket {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "ARP {:?}\n", self.operation)?;
        write!(f, "  Sender: {} ({})\n", 
               self.sender_proto_addr, self.sender_hw_addr)?;
        write!(f, "  Target: {} ({})", 
               self.target_proto_addr, self.target_hw_addr)
    }
}

// ARP Cache Entry
#[derive(Debug, Clone)]
struct ArpCacheEntry {
    mac_addr: MacAddress,
    timestamp: SystemTime,
    is_static: bool,
}

// ARP Cache
pub struct ArpCache {
    entries: HashMap<Ipv4Addr, ArpCacheEntry>,
    timeout: Duration,
}

impl ArpCache {
    pub fn new(timeout_seconds: u64) -> Self {
        ArpCache {
            entries: HashMap::new(),
            timeout: Duration::from_secs(timeout_seconds),
        }
    }
    
    // Add or update cache entry
    pub fn add(&mut self, ip: Ipv4Addr, mac: MacAddress, is_static: bool) {
        let entry = ArpCacheEntry {
            mac_addr: mac,
            timestamp: SystemTime::now(),
            is_static,
        };
        self.entries.insert(ip, entry);
    }
    
    // Lookup MAC address
    pub fn lookup(&self, ip: &Ipv4Addr) -> Option<MacAddress> {
        self.entries.get(ip).and_then(|entry| {
            if entry.is_static {
                return Some(entry.mac_addr);
            }
            
            // Check if entry has expired
            if let Ok(elapsed) = entry.timestamp.elapsed() {
                if elapsed < self.timeout {
                    return Some(entry.mac_addr);
                }
            }
            None
        })
    }
    
    // Remove expired entries
    pub fn cleanup(&mut self) {
        let now = SystemTime::now();
        self.entries.retain(|_, entry| {
            if entry.is_static {
                return true;
            }
            
            if let Ok(elapsed) = now.duration_since(entry.timestamp) {
                elapsed < self.timeout
            } else {
                false
            }
        });
    }
    
    // Remove specific entry
    pub fn remove(&mut self, ip: &Ipv4Addr) -> bool {
        self.entries.remove(ip).is_some()
    }
    
    // Clear all entries
    pub fn clear(&mut self) {
        self.entries.clear();
    }
    
    // Get all entries (for display)
    pub fn entries(&self) -> Vec<(Ipv4Addr, MacAddress, bool)> {
        self.entries
            .iter()
            .map(|(ip, entry)| (*ip, entry.mac_addr, entry.is_static))
            .collect()
    }
    
    // Print cache contents
    pub fn print(&self) {
        println!("\nARP Cache Contents:");
        println!("{:<15} {:<17} {:<10}", "IP Address", "MAC Address", "Type");
        println!("{}", "=".repeat(50));
        
        for (ip, entry) in &self.entries {
            println!("{:<15} {:<17} {:<10}",
                     ip,
                     entry.mac_addr,
                     if entry.is_static { "Static" } else { "Dynamic" });
        }
    }
}

// ARP Handler - manages ARP operations
pub struct ArpHandler {
    cache: ArpCache,
    local_ip: Ipv4Addr,
    local_mac: MacAddress,
}

impl ArpHandler {
    pub fn new(local_ip: Ipv4Addr, local_mac: MacAddress, cache_timeout: u64) -> Self {
        ArpHandler {
            cache: ArpCache::new(cache_timeout),
            local_ip,
            local_mac,
        }
    }
    
    // Handle incoming ARP packet
    pub fn handle_packet(&mut self, packet: &ArpPacket) -> Option<ArpPacket> {
        println!("Received ARP packet:\n{}", packet);
        
        // Update cache with sender's information
        self.cache.add(
            packet.sender_proto_addr,
            packet.sender_hw_addr,
            false,
        );
        
        // Check if packet is for us
        if packet.target_proto_addr != self.local_ip {
            return None;
        }
        
        match packet.operation {
            ArpOperation::Request => {
                // Send ARP reply
                Some(ArpPacket::new_reply(
                    self.local_mac,
                    self.local_ip,
                    packet.sender_hw_addr,
                    packet.sender_proto_addr,
                ))
            }
            ArpOperation::Reply => {
                // Just update cache (already done above)
                None
            }
        }
    }
    
    // Resolve IP to MAC (would trigger ARP request in real implementation)
    pub fn resolve(&mut self, ip: Ipv4Addr) -> Option<MacAddress> {
        if let Some(mac) = self.cache.lookup(&ip) {
            return Some(mac);
        }
        
        // In real implementation, send ARP request here
        println!("Would send ARP request for {}", ip);
        None
    }
    
    // Send gratuitous ARP
    pub fn send_gratuitous(&self) -> ArpPacket {
        ArpPacket::new_gratuitous(self.local_mac, self.local_ip)
    }
    
    // Get cache reference
    pub fn cache(&self) -> &ArpCache {
        &self.cache
    }
    
    // Get mutable cache reference
    pub fn cache_mut(&mut self) -> &mut ArpCache {
        &mut self.cache
    }
}

// Example: ARP Spoof Detection
pub struct ArpSpoofDetector {
    known_mappings: HashMap<Ipv4Addr, MacAddress>,
}

impl ArpSpoofDetector {
    pub fn new() -> Self {
        ArpSpoofDetector {
            known_mappings: HashMap::new(),
        }
    }
    
    // Check if packet is potentially malicious
    pub fn check_packet(&mut self, packet: &ArpPacket) -> bool {
        let ip = packet.sender_proto_addr;
        let mac = packet.sender_hw_addr;
        
        if let Some(&known_mac) = self.known_mappings.get(&ip) {
            if known_mac != mac {
                println!("⚠️  ARP Spoof detected!");
                println!("   IP: {}", ip);
                println!("   Known MAC: {}", known_mac);
                println!("   Received MAC: {}", mac);
                return false; // Potential spoof
            }
        } else {
            // First time seeing this IP, record it
            self.known_mappings.insert(ip, mac);
        }
        
        true // OK
    }
}

// Example usage and tests
fn main() {
    println!("=== ARP Protocol Implementation Demo ===\n");
    
    // Create local MAC and IP
    let local_mac = MacAddress::new([0x00, 0x11, 0x22, 0x33, 0x44, 0x55]);
    let local_ip = "192.168.1.10".parse().unwrap();
    
    // Create ARP handler
    let mut handler = ArpHandler::new(local_ip, local_mac, 300);
    
    // Add some static entries
    let gateway_mac = MacAddress::new([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]);
    let gateway_ip = "192.168.1.1".parse().unwrap();
    handler.cache_mut().add(gateway_ip, gateway_mac, true);
    
    // Demo 1: ARP Request
    println!("--- Demo 1: ARP Request ---");
    let target_ip: Ipv4Addr = "192.168.1.100".parse().unwrap();
    let request = ArpPacket::new_request(local_mac, local_ip, target_ip);
    println!("{}\n", request);
    
    // Serialize and deserialize
    let bytes = request.to_bytes();
    println!("Serialized to {} bytes", bytes.len());
    let parsed = ArpPacket::from_bytes(&bytes).unwrap();
    println!("Deserialized successfully\n");
    
    // Demo 2: ARP Reply
    println!("--- Demo 2: ARP Reply ---");
    let reply = ArpPacket::new_reply(
        MacAddress::new([0x11, 0x22, 0x33, 0x44, 0x55, 0x66]),
        target_ip,
        local_mac,
        local_ip,
    );
    println!("{}\n", reply);
    
    // Demo 3: Gratuitous ARP
    println!("--- Demo 3: Gratuitous ARP ---");
    let garp = handler.send_gratuitous();
    println!("{}", garp);
    println!("Is Gratuitous: {}\n", garp.is_gratuitous());
    
    // Demo 4: Handle incoming packet
    println!("--- Demo 4: Handle Incoming ARP Request ---");
    let incoming_request = ArpPacket::new_request(
        MacAddress::new([0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb]),
        "192.168.1.50".parse().unwrap(),
        local_ip,
    );
    
    if let Some(response) = handler.handle_packet(&incoming_request) {
        println!("\nGenerated response:");
        println!("{}\n", response);
    }
    
    // Demo 5: Cache operations
    println!("--- Demo 5: ARP Cache ---");
    handler.cache().print();
    
    // Demo 6: Resolve IP
    println!("\n--- Demo 6: IP Resolution ---");
    if let Some(mac) = handler.resolve(gateway_ip) {
        println!("Resolved {} -> {}", gateway_ip, mac);
    }
    
    if handler.resolve("192.168.1.200".parse().unwrap()).is_none() {
        println!("192.168.1.200 not in cache (would send ARP request)");
    }
    
    // Demo 7: Spoof detection
    println!("\n--- Demo 7: ARP Spoof Detection ---");
    let mut detector = ArpSpoofDetector::new();
    
    let legit_packet = ArpPacket::new_reply(
        gateway_mac,
        gateway_ip,
        local_mac,
        local_ip,
    );
    detector.check_packet(&legit_packet);
    
    // Simulate spoof attempt
    let spoof_packet = ArpPacket::new_reply(
        MacAddress::new([0xff, 0xff, 0xff, 0xff, 0xff, 0xff]),
        gateway_ip, // Same IP, different MAC
        local_mac,
        local_ip,
    );
    detector.check_packet(&spoof_packet);
    
    println!("\n=== Demo Complete ===");
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_arp_packet_serialization() {
        let mac = MacAddress::new([0x00, 0x11, 0x22, 0x33, 0x44, 0x55]);
        let ip = "192.168.1.1".parse().unwrap();
        let packet = ArpPacket::new_request(mac, ip, "192.168.1.2".parse().unwrap());
        
        let bytes = packet.to_bytes();
        let parsed = ArpPacket::from_bytes(&bytes).unwrap();
        
        assert_eq!(parsed.operation, ArpOperation::Request);
        assert_eq!(parsed.sender_hw_addr, mac);
        assert_eq!(parsed.sender_proto_addr, ip);
    }
    
    #[test]
    fn test_cache_operations() {
        let mut cache = ArpCache::new(60);
        let mac = MacAddress::new([0x00, 0x11, 0x22, 0x33, 0x44, 0x55]);
        let ip = "192.168.1.1".parse().unwrap();
        
        cache.add(ip, mac, false);
        assert_eq!(cache.lookup(&ip), Some(mac));
        
        cache.remove(&ip);
        assert_eq!(cache.lookup(&ip), None);
    }
    
    #[test]
    fn test_gratuitous_arp_detection() {
        let mac = MacAddress::new([0x00, 0x11, 0x22, 0x33, 0x44, 0x55]);
        let ip = "192.168.1.1".parse().unwrap();
        let garp = ArpPacket::new_gratuitous(mac, ip);
        
        assert!(garp.is_gratuitous());
        assert_eq!(garp.sender_proto_addr, garp.target_proto_addr);
    }
}
