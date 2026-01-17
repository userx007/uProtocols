use std::collections::{HashMap, VecDeque};
use std::net::{UdpSocket, SocketAddr};
use std::time::{Duration, Instant};
use std::io::{self, ErrorKind};

const MAX_PACKET_SIZE: usize = 1400;
const WINDOW_SIZE: usize = 64;
const TIMEOUT: Duration = Duration::from_millis(100);
const MAX_RETRIES: u8 = 5;

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct PacketHeader {
    sequence: u32,
    ack: u32,
    ack_bitfield: u64,
    timestamp_us: u64,
    data_length: u16,
    flags: u8,
    reserved: u8,
}

#[derive(Debug, Clone)]
struct Packet {
    header: PacketHeader,
    data: Vec<u8>,
}

impl Packet {
    fn new(sequence: u32, data: &[u8]) -> Self {
        Self {
            header: PacketHeader {
                sequence,
                ack: 0,
                ack_bitfield: 0,
                timestamp_us: 0,
                data_length: data.len() as u16,
                flags: 0,
                reserved: 0,
            },
            data: data.to_vec(),
        }
    }
    
    fn to_bytes(&self) -> Vec<u8> {
        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &self.header as *const _ as *const u8,
                std::mem::size_of::<PacketHeader>(),
            )
        };
        
        let mut bytes = Vec::with_capacity(header_bytes.len() + self.data.len());
        bytes.extend_from_slice(header_bytes);
        bytes.extend_from_slice(&self.data);
        bytes
    }
    
    fn from_bytes(bytes: &[u8]) -> io::Result<Self> {
        if bytes.len() < std::mem::size_of::<PacketHeader>() {
            return Err(io::Error::new(ErrorKind::InvalidData, "Packet too small"));
        }
        
        let header = unsafe {
            std::ptr::read_unaligned(bytes.as_ptr() as *const PacketHeader)
        };
        
        let data_start = std::mem::size_of::<PacketHeader>();
        let data_end = data_start + header.data_length as usize;
        
        if bytes.len() < data_end {
            return Err(io::Error::new(ErrorKind::InvalidData, "Incomplete packet"));
        }
        
        Ok(Self {
            header,
            data: bytes[data_start..data_end].to_vec(),
        })
    }
}

#[derive(Debug)]
struct PendingPacket {
    packet: Packet,
    send_time: Instant,
    retry_count: u8,
    acked: bool,
}

#[derive(Debug, Default)]
struct Statistics {
    packets_sent: u64,
    packets_received: u64,
    retransmissions: u64,
    acks_received: u64,
    packets_dropped: u64,
}

pub struct ReliableUdp {
    socket: UdpSocket,
    remote_addr: SocketAddr,
    
    // Send state
    send_sequence: u32,
    send_window: VecDeque<PendingPacket>,
    
    // Receive state
    recv_sequence: u32,
    recv_ack_bitfield: u64,
    recv_buffer: HashMap<u32, Packet>,
    
    // Statistics
    stats: Statistics,
}

impl ReliableUdp {
    pub fn new(local_addr: &str, remote_addr: &str) -> io::Result<Self> {
        let socket = UdpSocket::bind(local_addr)?;
        socket.set_nonblocking(true)?;
        socket.set_read_timeout(Some(Duration::from_millis(10)))?;
        
        let remote: SocketAddr = remote_addr.parse()
            .map_err(|e| io::Error::new(ErrorKind::InvalidInput, e))?;
        
        let mut send_window = VecDeque::with_capacity(WINDOW_SIZE);
        for _ in 0..WINDOW_SIZE {
            send_window.push_back(PendingPacket {
                packet: Packet::new(0, &[]),
                send_time: Instant::now(),
                retry_count: 0,
                acked: true,
            });
        }
        
        Ok(Self {
            socket,
            remote_addr: remote,
            send_sequence: 0,
            send_window,
            recv_sequence: 0,
            recv_ack_bitfield: 0,
            recv_buffer: HashMap::new(),
            stats: Statistics::default(),
        })
    }
    
    pub fn send(&mut self, data: &[u8]) -> io::Result<()> {
        if data.len() > MAX_PACKET_SIZE {
            return Err(io::Error::new(
                ErrorKind::InvalidInput,
                "Data exceeds max packet size"
            ));
        }
        
        let slot = (self.send_sequence as usize) % WINDOW_SIZE;
        let pending = &self.send_window[slot];
        
        // Check if window is full
        if !pending.acked && 
           pending.packet.header.sequence >= self.send_sequence.saturating_sub(WINDOW_SIZE as u32) {
            return Err(io::Error::new(ErrorKind::WouldBlock, "Send window full"));
        }
        
        // Create new packet
        let mut packet = Packet::new(self.send_sequence, data);
        packet.header.ack = self.recv_sequence;
        packet.header.ack_bitfield = self.recv_ack_bitfield;
        packet.header.timestamp_us = Self::get_timestamp_us();
        
        self.send_sequence += 1;
        
        // Transmit packet
        self.transmit_packet(&packet)?;
        
        // Store in send window
        let pending = &mut self.send_window[slot];
        pending.packet = packet;
        pending.send_time = Instant::now();
        pending.retry_count = 0;
        pending.acked = false;
        
        self.stats.packets_sent += 1;
        
        Ok(())
    }
    
    pub fn receive(&mut self) -> io::Result<Option<Vec<u8>>> {
        let mut buffer = vec![0u8; MAX_PACKET_SIZE + 256];
        
        match self.socket.recv_from(&mut buffer) {
            Ok((size, _addr)) => {
                let packet = Packet::from_bytes(&buffer[..size])?;
                
                // Process ACKs
                self.process_acknowledgments(&packet.header);
                
                // Handle received data
                if !packet.data.is_empty() {
                    self.stats.packets_received += 1;
                    return Ok(self.process_received_packet(packet));
                }
                
                Ok(None)
            }
            Err(e) if e.kind() == ErrorKind::WouldBlock => Ok(None),
            Err(e) => Err(e),
        }
    }
    
    pub fn process_retransmissions(&mut self) -> io::Result<()> {
        let now = Instant::now();
        
        for i in 0..WINDOW_SIZE {
            let pending = &self.send_window[i];
            
            if pending.acked || pending.packet.header.sequence >= self.send_sequence {
                continue;
            }
            
            let elapsed = now.duration_since(pending.send_time);
            
            if elapsed > TIMEOUT {
                if pending.retry_count >= MAX_RETRIES {
                    println!("Packet {} exceeded max retries, dropping",
                            pending.packet.header.sequence);
                    self.send_window[i].acked = true;
                    self.stats.packets_dropped += 1;
                    continue;
                }
                
                // Update timestamp and retransmit
                let mut packet = pending.packet.clone();
                packet.header.timestamp_us = Self::get_timestamp_us();
                
                if let Err(e) = self.transmit_packet(&packet) {
                    eprintln!("Retransmission failed: {}", e);
                    continue;
                }
                
                self.send_window[i].packet = packet;
                self.send_window[i].send_time = now;
                self.send_window[i].retry_count += 1;
                self.stats.retransmissions += 1;
                
                println!("Retransmitting seq={} (attempt {})",
                        self.send_window[i].packet.header.sequence,
                        self.send_window[i].retry_count);
            }
        }
        
        Ok(())
    }
    
    pub fn stats(&self) -> &Statistics {
        &self.stats
    }
    
    pub fn print_stats(&self) {
        println!("\n=== Reliable UDP Statistics ===");
        println!("Packets sent: {}", self.stats.packets_sent);
        println!("Packets received: {}", self.stats.packets_received);
        println!("Retransmissions: {}", self.stats.retransmissions);
        println!("ACKs received: {}", self.stats.acks_received);
        println!("Packets dropped: {}", self.stats.packets_dropped);
        
        if self.stats.packets_sent > 0 {
            let loss_rate = (100.0 * self.stats.retransmissions as f64) 
                          / self.stats.packets_sent as f64;
            println!("Loss rate: {:.2}%", loss_rate);
        }
    }
    
    fn transmit_packet(&self, packet: &Packet) -> io::Result<()> {
        let bytes = packet.to_bytes();
        self.socket.send_to(&bytes, self.remote_addr)?;
        Ok(())
    }
    
    fn process_acknowledgments(&mut self, header: &PacketHeader) {
        self.stats.acks_received += 1;
        
        for pending in &mut self.send_window {
            if pending.acked {
                continue;
            }
            
            let seq = pending.packet.header.sequence;
            
            // Direct ACK
            if seq == header.ack {
                pending.acked = true;
            }
            // Selective ACK bitfield
            else if seq < header.ack && (header.ack - seq) <= 64 {
                let bit_pos = header.ack - seq - 1;
                if (header.ack_bitfield & (1u64 << bit_pos)) != 0 {
                    pending.acked = true;
                }
            }
        }
    }
    
    fn process_received_packet(&mut self, packet: Packet) -> Option<Vec<u8>> {
        let seq = packet.header.sequence;
        
        if seq == self.recv_sequence {
            // In-order packet - deliver immediately
            self.recv_sequence += 1;
            self.recv_ack_bitfield = 0;
            
            // Try to deliver buffered packets
            let mut result = vec![packet.data];
            while let Some(buffered) = self.recv_buffer.remove(&self.recv_sequence) {
                result.push(buffered.data);
                self.recv_sequence += 1;
            }
            
            // Return first packet (simplified)
            return Some(result.into_iter().next().unwrap());
        }
        else if seq > self.recv_sequence && seq < self.recv_sequence + 64 {
            // Out-of-order packet - buffer it
            let bit_pos = seq - self.recv_sequence - 1;
            self.recv_ack_bitfield |= 1u64 << bit_pos;
            self.recv_buffer.insert(seq, packet);
        }
        
        None
    }
    
    fn get_timestamp_us() -> u64 {
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_micros() as u64
    }
}

fn main() -> io::Result<()> {
    let mut rudp = ReliableUdp::new("127.0.0.1:0", "127.0.0.1:9999")?;
    
    let messages = vec![
        "Hello from Rust reliable UDP!",
        "Second message with reliability",
        "Third packet incoming",
        "Final test message",
    ];
    
    // Send messages
    for (i, msg) in messages.iter().enumerate() {
        match rudp.send(msg.as_bytes()) {
            Ok(_) => println!("Sent message {}", i),
            Err(e) => eprintln!("Failed to send message {}: {}", i, e),
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    
    // Process for a while
    for _ in 0..100 {
        if let Some(data) = rudp.receive()? {
            if let Ok(msg) = String::from_utf8(data) {
                println!("Received: {}", msg);
            }
        }
        
        rudp.process_retransmissions()?;
        std::thread::sleep(Duration::from_millis(10));
    }
    
    rudp.print_stats();
    
    Ok(())
}