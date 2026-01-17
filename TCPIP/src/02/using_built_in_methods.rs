use std::net::Ipv4Addr;

fn main() {
    println!("=== Rust Network Byte Order Demonstration ===\n");
    
    // Rust provides built-in methods for byte order conversion
    let host_port: u16 = 8080;
    let host_ip: u32 = 0xC0A80001; // 192.168.0.1
    
    // Convert to network byte order (big-endian)
    let net_port = host_port.to_be();
    let net_ip = host_ip.to_be();
    
    println!("Host Byte Order:");
    println!("  Port: {} (0x{:04X})", host_port, host_port);
    println!("  IP:   0x{:08X}\n", host_ip);
    
    println!("Network Byte Order:");
    println!("  Port: {} (0x{:04X})", net_port, net_port);
    println!("  IP:   0x{:08X}\n", net_ip);
    
    // Convert back to host byte order
    let converted_port = u16::from_be(net_port);
    let converted_ip = u32::from_be(net_ip);
    
    println!("Converted Back:");
    println!("  Port: {} (0x{:04X})", converted_port, converted_port);
    println!("  IP:   0x{:08X}\n", converted_ip);
    
    // Verify conversions
    assert_eq!(converted_port, host_port);
    assert_eq!(converted_ip, host_ip);
    println!("✓ Conversion successful!\n");
    
    // Rust also provides to_le() and from_le() for little-endian
    let little_endian = host_port.to_le();
    println!("Little Endian Conversion:");
    println!("  Port: {} (0x{:04X})", little_endian, little_endian);
    
    // Working with IP addresses
    let ip = Ipv4Addr::new(192, 168, 0, 1);
    let ip_u32 = u32::from(ip); // Automatically in network byte order
    
    println!("\nIP Address Handling:");
    println!("  IP: {}", ip);
    println!("  As u32 (network order): 0x{:08X}", ip_u32);
    
    // Convert back
    let reconstructed_ip = Ipv4Addr::from(ip_u32);
    println!("  Reconstructed: {}", reconstructed_ip);
    assert_eq!(ip, reconstructed_ip);
}