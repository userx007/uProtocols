use std::net::{TcpListener, TcpStream, SocketAddr, Ipv4Addr};
use std::io::{self, Read, Write};

// Custom protocol header with network byte order fields
#[repr(C)]
#[derive(Debug, Clone, Copy)]
struct PacketHeader {
    magic: u32,        // Protocol identifier
    version: u16,      // Protocol version
    length: u16,       // Payload length
    sequence: u32,     // Packet sequence number
}

impl PacketHeader {
    fn new(length: u16, sequence: u32) -> Self {
        PacketHeader {
            magic: 0x50415753,  // "PAWS" in ASCII
            version: 1,
            length,
            sequence,
        }
    }
    
    // Convert all fields to network byte order
    fn to_network_order(&self) -> Self {
        PacketHeader {
            magic: self.magic.to_be(),
            version: self.version.to_be(),
            length: self.length.to_be(),
            sequence: self.sequence.to_be(),
        }
    }
    
    // Convert all fields from network byte order
    fn from_network_order(net_header: &PacketHeader) -> Self {
        PacketHeader {
            magic: u32::from_be(net_header.magic),
            version: u16::from_be(net_header.version),
            length: u16::from_be(net_header.length),
            sequence: u32::from_be(net_header.sequence),
        }
    }
    
    // Convert to bytes for transmission
    fn to_bytes(&self) -> Vec<u8> {
        let net_order = self.to_network_order();
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&net_order.magic.to_ne_bytes());
        bytes.extend_from_slice(&net_order.version.to_ne_bytes());
        bytes.extend_from_slice(&net_order.length.to_ne_bytes());
        bytes.extend_from_slice(&net_order.sequence.to_ne_bytes());
        bytes
    }
}

fn demonstrate_server() -> io::Result<()> {
    let addr = SocketAddr::from((Ipv4Addr::LOCALHOST, 8080));
    
    println!("Server Configuration:");
    println!("  Bind address: {}", addr);
    println!("  IP (host order): {:?}", addr.ip());
    println!("  Port (host order): {}", addr.port());
    
    // Create the listener - Rust handles byte order conversions internally
    let listener = TcpListener::bind(addr)?;
    
    println!("  ✓ Server bound successfully");
    println!("  Listening on {}\n", addr);
    
    // In a real server, you'd accept connections here
    // For demonstration, we'll just show the setup
    drop(listener);
    
    Ok(())
}

fn demonstrate_packet_handling() {
    println!("=== Packet Header Demonstration ===");
    
    // Create a packet header
    let header = PacketHeader::new(256, 42);
    
    println!("\nOriginal Header (host byte order):");
    println!("  Magic:    0x{:08X}", header.magic);
    println!("  Version:  {}", header.version);
    println!("  Length:   {}", header.length);
    println!("  Sequence: {}", header.sequence);
    
    // Convert to network byte order
    let net_header = header.to_network_order();
    
    println!("\nNetwork Byte Order:");
    println!("  Magic:    0x{:08X}", net_header.magic);
    println!("  Version:  0x{:04X}", net_header.version);
    println!("  Length:   0x{:04X}", net_header.length);
    println!("  Sequence: 0x{:08X}", net_header.sequence);
    
    // Serialize to bytes
    let bytes = header.to_bytes();
    println!("\nSerialized bytes: {:02X?}", bytes);
    
    // Parse back from network order
    let parsed = PacketHeader::from_network_order(&net_header);
    
    println!("\nParsed Header (converted back):");
    println!("  Magic:    0x{:08X}", parsed.magic);
    println!("  Version:  {}", parsed.version);
    println!("  Length:   {}", parsed.length);
    println!("  Sequence: {}", parsed.sequence);
    
    // Verify correctness
    assert_eq!(header.magic, parsed.magic);
    assert_eq!(header.version, parsed.version);
    assert_eq!(header.length, parsed.length);
    assert_eq!(header.sequence, parsed.sequence);
    println!("\n✓ Round-trip conversion successful!");
}

fn main() -> io::Result<()> {
    println!("=== Rust Network Byte Order Socket Demo ===\n");
    
    println!("--- Server Setup ---");
    demonstrate_server()?;
    
    println!("--- Custom Protocol ---");
    demonstrate_packet_handling();
    
    Ok(())
}