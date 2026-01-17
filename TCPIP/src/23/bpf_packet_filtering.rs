// Cargo.toml dependencies:
// [dependencies]
// pcap = "1.1"
// pnet = "0.34"

use pcap::{Capture, Device, Active, Error};
use pnet::packet::ethernet::{EthernetPacket, EtherTypes};
use pnet::packet::ip::IpNextHeaderProtocols;
use pnet::packet::ipv4::Ipv4Packet;
use pnet::packet::tcp::TcpPacket;
use pnet::packet::udp::UdpPacket;
use pnet::packet::Packet;
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicBool, Ordering};

#[derive(Debug, Default)]
struct PacketStats {
    total: u64,
    tcp: u64,
    udp: u64,
    icmp: u64,
    other: u64,
    src_ips: HashMap<String, u64>,
}

impl PacketStats {
    fn new() -> Self {
        Self::default()
    }
    
    fn record_packet(&mut self, protocol: &str, src_ip: String) {
        self.total += 1;
        match protocol {
            "TCP" => self.tcp += 1,
            "UDP" => self.udp += 1,
            "ICMP" => self.icmp += 1,
            _ => self.other += 1,
        }
        *self.src_ips.entry(src_ip).or_insert(0) += 1;
    }
    
    fn print_summary(&self) {
        println!("\n=== Packet Capture Statistics ===");
        println!("Total packets: {}", self.total);
        println!("TCP: {} ({:.2}%)", self.tcp, 
                 (self.tcp as f64 / self.total as f64) * 100.0);
        println!("UDP: {} ({:.2}%)", self.udp,
                 (self.udp as f64 / self.total as f64) * 100.0);
        println!("ICMP: {} ({:.2}%)", self.icmp,
                 (self.icmp as f64 / self.total as f64) * 100.0);
        println!("Other: {} ({:.2}%)", self.other,
                 (self.other as f64 / self.total as f64) * 100.0);
        
        println!("\nTop 5 Source IPs:");
        let mut sorted_ips: Vec<_> = self.src_ips.iter().collect();
        sorted_ips.sort_by(|a, b| b.1.cmp(a.1));
        
        for (i, (ip, count)) in sorted_ips.iter().take(5).enumerate() {
            println!("  {}. {}: {} packets", i + 1, ip, count);
        }
    }
}

struct BpfCapture {
    capture: Capture<Active>,
    stats: Arc<Mutex<PacketStats>>,
    running: Arc<AtomicBool>,
}

impl BpfCapture {
    fn new(device_name: &str, filter: &str) -> Result<Self, Error> {
        let device = Device::lookup()
            .unwrap()
            .unwrap_or_else(|| {
                println!("No default device found, using first available");
                Device::list().unwrap()[0].clone()
            });
        
        println!("Opening device: {}", device.name);
        
        let mut cap = Capture::from_device(device)?
            .promisc(true)
            .snaplen(65535)
            .timeout(1000)
            .open()?;
        
        // Apply BPF filter
        cap.filter(filter, true)?;
        println!("Applied filter: {}", filter);
        
        Ok(BpfCapture {
            capture: cap,
            stats: Arc::new(Mutex::new(PacketStats::new())),
            running: Arc::new(AtomicBool::new(true)),
        })
    }
    
    fn process_packet(&self, data: &[u8]) {
        if let Some(ethernet) = EthernetPacket::new(data) {
            match ethernet.get_ethertype() {
                EtherTypes::Ipv4 => {
                    if let Some(ipv4) = Ipv4Packet::new(ethernet.payload()) {
                        let src_ip = ipv4.get_source().to_string();
                        let dst_ip = ipv4.get_destination().to_string();
                        
                        let protocol = match ipv4.get_next_level_protocol() {
                            IpNextHeaderProtocols::Tcp => {
                                if let Some(tcp) = TcpPacket::new(ipv4.payload()) {
                                    println!("TCP: {}:{} -> {}:{}", 
                                             src_ip, tcp.get_source(),
                                             dst_ip, tcp.get_destination());
                                    
                                    // Print TCP flags
                                    print!("  Flags: ");
                                    if tcp.get_flags() & 0x02 != 0 { print!("SYN "); }
                                    if tcp.get_flags() & 0x10 != 0 { print!("ACK "); }
                                    if tcp.get_flags() & 0x01 != 0 { print!("FIN "); }
                                    if tcp.get_flags() & 0x04 != 0 { print!("RST "); }
                                    println!();
                                }
                                "TCP"
                            },
                            IpNextHeaderProtocols::Udp => {
                                if let Some(udp) = UdpPacket::new(ipv4.payload()) {
                                    println!("UDP: {}:{} -> {}:{} (len: {})", 
                                             src_ip, udp.get_source(),
                                             dst_ip, udp.get_destination(),
                                             udp.get_length());
                                }
                                "UDP"
                            },
                            IpNextHeaderProtocols::Icmp => {
                                println!("ICMP: {} -> {}", src_ip, dst_ip);
                                "ICMP"
                            },
                            _ => {
                                println!("Other protocol: {} -> {}", src_ip, dst_ip);
                                "Other"
                            },
                        };
                        
                        if let Ok(mut stats) = self.stats.lock() {
                            stats.record_packet(protocol, src_ip);
                        }
                    }
                },
                EtherTypes::Ipv6 => {
                    println!("IPv6 packet (skipping detailed parsing)");
                },
                _ => {
                    println!("Non-IP packet: {:?}", ethernet.get_ethertype());
                }
            }
        }
    }
    
    fn start(&mut self, max_packets: Option<usize>) {
        println!("Starting packet capture...\n");
        
        let mut count = 0;
        while self.running.load(Ordering::Relaxed) {
            match self.capture.next_packet() {
                Ok(packet) => {
                    self.process_packet(packet.data);
                    count += 1;
                    
                    if let Some(max) = max_packets {
                        if count >= max {
                            break;
                        }
                    }
                },
                Err(pcap::Error::TimeoutExpired) => continue,
                Err(e) => {
                    eprintln!("Error reading packet: {}", e);
                    break;
                }
            }
        }
    }
    
    fn print_stats(&self) {
        if let Ok(stats) = self.stats.lock() {
            stats.print_summary();
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    
    let device = if args.len() > 1 {
        &args[1]
    } else {
        "any"
    };
    
    let filter = if args.len() > 2 {
        &args[2]
    } else {
        "tcp or udp"
    };
    
    println!("BPF Packet Capture in Rust");
    println!("==========================");
    
    let mut capture = BpfCapture::new(device, filter)?;
    
    // Setup Ctrl+C handler
    let running = Arc::clone(&capture.running);
    ctrlc::set_handler(move || {
        println!("\nStopping capture...");
        running.store(false, Ordering::Relaxed);
    }).expect("Error setting Ctrl+C handler");
    
    capture.start(Some(50)); // Capture 50 packets
    capture.print_stats();
    
    Ok(())
}

// To run:
// cargo build --release
// sudo ./target/release/bpf_packet_filtering [device] [filter]
// Example: sudo ./target/release/bpf_packet_filtering eth0 "port 80 or port 443"