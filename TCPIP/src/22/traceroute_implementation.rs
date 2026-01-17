use std::net::{IpAddr, Ipv4Addr, SocketAddr, ToSocketAddrs};
use std::time::{Duration, Instant};
use std::io;

const MAX_HOPS: u8 = 30;
const PACKET_SIZE: usize = 64;
const TIMEOUT: Duration = Duration::from_secs(2);
const DEST_PORT_BASE: u16 = 33434;

// ICMP header types
const ICMP_ECHOREPLY: u8 = 0;
const ICMP_DEST_UNREACH: u8 = 3;
const ICMP_TIME_EXCEEDED: u8 = 11;

fn send_probe(
    socket: &socket2::Socket,
    dest: &socket2::SockAddr,
    ttl: u8,
    seq: u32,
) -> io::Result<Instant> {
    // Set TTL for this probe
    socket.set_ttl(ttl as u32)?;
    
    // Create probe packet with sequence number
    let mut packet = vec![0u8; PACKET_SIZE];
    packet[0..4].copy_from_slice(&seq.to_be_bytes());
    
    let send_time = Instant::now();
    socket.send_to(&packet, dest)?;
    
    Ok(send_time)
}

fn receive_icmp_response(
    socket: &socket2::Socket,
    send_time: Instant,
) -> io::Result<(Ipv4Addr, Duration, bool)> {
    let mut buffer = [0u8; 1024];
    
    let (len, addr) = socket.recv_from(&mut buffer)?;
    let recv_time = Instant::now();
    let rtt = recv_time.duration_since(send_time);
    
    if len < 28 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Packet too short",
        ));
    }
    
    // Parse IP header
    let ip_header_len = ((buffer[0] & 0x0F) * 4) as usize;
    
    if len < ip_header_len + 8 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "ICMP packet too short",
        ));
    }
    
    // Extract source IP from IP header (bytes 12-15)
    let src_ip = Ipv4Addr::new(
        buffer[12],
        buffer[13],
        buffer[14],
        buffer[15],
    );
    
    // Parse ICMP header
    let icmp_type = buffer[ip_header_len];
    
    // Determine if we reached the destination
    let is_destination = match icmp_type {
        ICMP_TIME_EXCEEDED => false,      // Intermediate hop
        ICMP_DEST_UNREACH => true,        // Destination reached
        ICMP_ECHOREPLY => true,           // Destination reached (if using ICMP)
        _ => false,
    };
    
    Ok((src_ip, rtt, is_destination))
}

fn resolve_hostname(ip: Ipv4Addr) -> String {
    // Attempt reverse DNS lookup
    match dns_lookup::lookup_addr(&IpAddr::V4(ip)) {
        Ok(hostname) => format!("{} ({})", hostname, ip),
        Err(_) => format!("{}", ip),
    }
}

fn traceroute(dest_ip: Ipv4Addr, dest_name: &str) -> io::Result<()> {
    println!(
        "traceroute to {} ({}), {} hops max, {} byte packets",
        dest_name, dest_ip, MAX_HOPS, PACKET_SIZE
    );
    
    // Create UDP socket for sending probes
    let udp_socket = socket2::Socket::new(
        socket2::Domain::IPV4,
        socket2::Type::DGRAM,
        Some(socket2::Protocol::UDP),
    )?;
    
    // Create raw ICMP socket for receiving responses
    let icmp_socket = socket2::Socket::new(
        socket2::Domain::IPV4,
        socket2::Type::RAW,
        Some(socket2::Protocol::ICMPV4),
    )?;
    
    icmp_socket.set_read_timeout(Some(TIMEOUT))?;
    
    let mut reached_dest = false;
    
    for ttl in 1..=MAX_HOPS {
        if reached_dest {
            break;
        }
        
        print!("{:2}  ", ttl);
        
        let mut hop_responded = false;
        let mut hop_ip: Option<Ipv4Addr> = None;
        
        // Send 3 probes per hop
        for probe in 0..3 {
            let seq = (ttl as u32) * 1000 + probe;
            let port = DEST_PORT_BASE + seq as u16;
            
            let dest_sock_addr = socket2::SockAddr::from(
                SocketAddr::new(IpAddr::V4(dest_ip), port)
            );
            
            match send_probe(&udp_socket, &dest_sock_addr, ttl, seq) {
                Ok(send_time) => {
                    match receive_icmp_response(&icmp_socket, send_time) {
                        Ok((from_ip, rtt, is_dest)) => {
                            // Print hostname on first response from this hop
                            if !hop_responded {
                                let hostname = resolve_hostname(from_ip);
                                print!("{}  ", hostname);
                                hop_responded = true;
                                hop_ip = Some(from_ip);
                            }
                            
                            print!("{:.3} ms  ", rtt.as_secs_f64() * 1000.0);
                            
                            if is_dest || from_ip == dest_ip {
                                reached_dest = true;
                            }
                        }
                        Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                            print!("* ");
                        }
                        Err(_) => {
                            print!("* ");
                        }
                    }
                }
                Err(_) => {
                    print!("* ");
                }
            }
        }
        
        println!();
    }
    
    Ok(())
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
    
    let dest_ip = match addr.ip() {
        IpAddr::V4(ip) => ip,
        _ => {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "IPv6 not supported",
            ))
        }
    };
    
    traceroute(dest_ip, host)?;
    
    Ok(())
}

// Add to Cargo.toml:
// [dependencies]
// socket2 = "0.5"
// dns-lookup = "2.0"