use std::net::{IpAddr, ToSocketAddrs};
use std::time::{Duration, Instant};
use std::io;

// ICMP Echo Request packet structure
#[repr(C, packed)]
struct IcmpEchoPacket {
    icmp_type: u8,     // Type 8 for echo request
    code: u8,          // Code 0
    checksum: u16,     // Checksum
    identifier: u16,   // Process ID
    sequence: u16,     // Sequence number
    data: [u8; 56],    // Payload data
}

impl IcmpEchoPacket {
    fn new(identifier: u16, sequence: u16) -> Self {
        let mut packet = IcmpEchoPacket {
            icmp_type: 8,
            code: 0,
            checksum: 0,
            identifier: identifier.to_be(),
            sequence: sequence.to_be(),
            data: [0u8; 56],
        };
        
        // Fill data with pattern
        for (i, byte) in packet.data.iter_mut().enumerate() {
            *byte = i as u8;
        }
        
        packet.checksum = packet.calculate_checksum();
        packet
    }
    
    fn calculate_checksum(&self) -> u16 {
        let bytes = unsafe {
            std::slice::from_raw_parts(
                self as *const _ as *const u8,
                std::mem::size_of::<IcmpEchoPacket>(),
            )
        };
        
        let mut sum: u32 = 0;
        let mut i = 0;
        
        // Sum 16-bit words
        while i < bytes.len() - 1 {
            let word = ((bytes[i] as u32) << 8) | (bytes[i + 1] as u32);
            sum += word;
            i += 2;
        }
        
        // Add remaining byte if odd length
        if i < bytes.len() {
            sum += (bytes[i] as u32) << 8;
        }
        
        // Fold 32-bit sum to 16 bits
        while sum >> 16 != 0 {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        
        !sum as u16
    }
    
    fn as_bytes(&self) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts(
                self as *const _ as *const u8,
                std::mem::size_of::<IcmpEchoPacket>(),
            )
        }
    }
}

// ICMP Echo Reply parsing
#[repr(C, packed)]
struct IcmpEchoReply {
    icmp_type: u8,
    code: u8,
    checksum: u16,
    identifier: u16,
    sequence: u16,
}

fn send_ping(
    socket: &socket2::Socket,
    addr: &socket2::SockAddr,
    identifier: u16,
    sequence: u16,
) -> io::Result<Instant> {
    let packet = IcmpEchoPacket::new(identifier, sequence);
    let send_time = Instant::now();
    
    socket.send_to(packet.as_bytes(), addr)?;
    
    Ok(send_time)
}

fn receive_ping(
    socket: &socket2::Socket,
    identifier: u16,
    send_time: Instant,
) -> io::Result<(u16, Duration)> {
    let mut buffer = [0u8; 1024];
    
    let (len, _) = socket.recv_from(&mut buffer)?;
    let recv_time = Instant::now();
    
    if len < 28 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Packet too short",
        ));
    }
    
    // Skip IP header (typically 20 bytes, but check IHL field)
    let ip_header_len = ((buffer[0] & 0x0F) * 4) as usize;
    
    if len < ip_header_len + 8 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "ICMP packet too short",
        ));
    }
    
    // Parse ICMP header
    let icmp_data = &buffer[ip_header_len..];
    let icmp_type = icmp_data[0];
    let icmp_id = u16::from_be_bytes([icmp_data[4], icmp_data[5]]);
    let icmp_seq = u16::from_be_bytes([icmp_data[6], icmp_data[7]]);
    
    // Verify it's an echo reply for our request
    if icmp_type == 0 && icmp_id == identifier {
        let rtt = recv_time.duration_since(send_time);
        Ok((icmp_seq, rtt))
    } else {
        Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Not our echo reply",
        ))
    }
}

fn main() -> io::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() != 2 {
        eprintln!("Usage: {} <hostname>", args[0]);
        std::process::exit(1);
    }
    
    let host = &args[1];
    
    // Resolve hostname to IP address
    let addr = format!("{}:0", host)
        .to_socket_addrs()?
        .find(|addr| addr.is_ipv4())
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "Host not found"))?;
    
    println!("PING {} ({}): 64 data bytes", host, addr.ip());
    
    // Create raw ICMP socket (requires CAP_NET_RAW capability)
    let socket = socket2::Socket::new(
        socket2::Domain::IPV4,
        socket2::Type::RAW,
        Some(socket2::Protocol::ICMPV4),
    )?;
    
    // Set receive timeout
    socket.set_read_timeout(Some(Duration::from_secs(2)))?;
    
    let identifier = std::process::id() as u16;
    let sock_addr = socket2::SockAddr::from(addr);
    
    let mut packets_sent = 0;
    let mut packets_received = 0;
    let mut total_rtt = Duration::ZERO;
    
    for seq in 0..4 {
        let send_time = send_ping(&socket, &sock_addr, identifier, seq)?;
        packets_sent += 1;
        
        match receive_ping(&socket, identifier, send_time) {
            Ok((recv_seq, rtt)) => {
                packets_received += 1;
                total_rtt += rtt;
                println!(
                    "64 bytes from {}: icmp_seq={} ttl=64 time={:.3} ms",
                    addr.ip(),
                    recv_seq,
                    rtt.as_secs_f64() * 1000.0
                );
            }
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                println!("Request timeout for icmp_seq {}", seq);
            }
            Err(e) => {
                eprintln!("Error receiving: {}", e);
            }
        }
        
        std::thread::sleep(Duration::from_secs(1));
    }
    
    // Print statistics
    println!("\n--- {} ping statistics ---", host);
    println!(
        "{} packets transmitted, {} received, {:.0}% packet loss",
        packets_sent,
        packets_received,
        100.0 * (packets_sent - packets_received) as f64 / packets_sent as f64
    );
    
    if packets_received > 0 {
        let avg_rtt = total_rtt / packets_received;
        println!(
            "rtt min/avg/max = {:.3}/{:.3}/{:.3} ms",
            avg_rtt.as_secs_f64() * 1000.0,
            avg_rtt.as_secs_f64() * 1000.0,
            avg_rtt.as_secs_f64() * 1000.0
        );
    }
    
    Ok(())
}

// Add to Cargo.toml:
// [dependencies]
// socket2 = "0.5"