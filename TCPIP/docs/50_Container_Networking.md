# Container Networking: Virtual Interfaces, Network Namespaces, and Overlay Networks

## Overview

Container networking is a sophisticated networking paradigm that enables isolated application environments to communicate securely and efficiently. At its core, container networking relies on three fundamental technologies: virtual network interfaces, network namespaces, and overlay networks. These technologies work together to provide network isolation, connectivity, and scalability for containerized applications.

## Network Namespaces

Network namespaces are a Linux kernel feature that provides complete isolation of network resources. Each namespace has its own network stack, including network interfaces, routing tables, firewall rules, and socket connections. This isolation is fundamental to container security and multi-tenancy.

When a network namespace is created, it starts with only a loopback interface. Containers can then have their own private network interfaces, IP addresses, and routing configurations without interfering with other containers or the host system.

### C Example: Creating and Managing Network Namespaces

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define STACK_SIZE (1024 * 1024)

// Function to run in new network namespace
static int child_func(void* arg) {
    printf("Child process in new network namespace (PID: %d)\n", getpid());
    
    // Create a socket to demonstrate namespace isolation
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    // List network interfaces in this namespace
    struct ifreq ifr;
    for (int i = 1; i < 10; i++) {
        ifr.ifr_ifindex = i;
        if (ioctl(sock, SIOCGIFNAME, &ifr) == 0) {
            printf("  Interface %d: %s\n", i, ifr.ifr_name);
        }
    }
    
    close(sock);
    
    // Keep namespace alive for inspection
    printf("Namespace created. Press Enter to exit...\n");
    getchar();
    
    return 0;
}

int main() {
    char* stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return 1;
    }
    
    printf("Parent process (PID: %d)\n", getpid());
    
    // Create child process with new network namespace
    pid_t pid = clone(child_func, stack + STACK_SIZE,
                     CLONE_NEWNET | SIGCHLD, NULL);
    
    if (pid == -1) {
        perror("clone");
        free(stack);
        return 1;
    }
    
    printf("Created child with PID: %d in new network namespace\n", pid);
    printf("Parent network namespace remains unchanged\n");
    
    // Wait for child to complete
    waitpid(pid, NULL, 0);
    
    free(stack);
    return 0;
}
```

### Rust Example: Network Namespace Management

```rust
use std::process::Command;
use std::fs;
use std::io::{self, Write};

/// Represents a network namespace
pub struct NetworkNamespace {
    name: String,
}

impl NetworkNamespace {
    /// Create a new network namespace
    pub fn create(name: &str) -> io::Result<Self> {
        // Create namespace using ip netns command
        let output = Command::new("ip")
            .args(&["netns", "add", name])
            .output()?;
        
        if !output.status.success() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Failed to create namespace: {}", 
                       String::from_utf8_lossy(&output.stderr))
            ));
        }
        
        println!("Created network namespace: {}", name);
        Ok(NetworkNamespace {
            name: name.to_string(),
        })
    }
    
    /// Execute a command inside the namespace
    pub fn exec(&self, command: &str, args: &[&str]) -> io::Result<String> {
        let mut cmd_args = vec!["netns", "exec", &self.name, command];
        cmd_args.extend_from_slice(args);
        
        let output = Command::new("ip")
            .args(&cmd_args)
            .output()?;
        
        if output.status.success() {
            Ok(String::from_utf8_lossy(&output.stdout).to_string())
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr).to_string()
            ))
        }
    }
    
    /// List interfaces in the namespace
    pub fn list_interfaces(&self) -> io::Result<()> {
        let output = self.exec("ip", &["link", "show"])?;
        println!("Interfaces in namespace '{}':\n{}", self.name, output);
        Ok(())
    }
    
    /// Delete the namespace
    pub fn delete(&self) -> io::Result<()> {
        let output = Command::new("ip")
            .args(&["netns", "delete", &self.name])
            .output()?;
        
        if output.status.success() {
            println!("Deleted network namespace: {}", self.name);
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr).to_string()
            ))
        }
    }
}

impl Drop for NetworkNamespace {
    fn drop(&mut self) {
        let _ = self.delete();
    }
}

fn main() -> io::Result<()> {
    println!("=== Network Namespace Demo ===\n");
    
    // Create a new network namespace
    let ns = NetworkNamespace::create("demo_ns")?;
    
    // List interfaces (should only show loopback initially)
    ns.list_interfaces()?;
    
    // Bring up loopback interface
    println!("\nBringing up loopback interface...");
    ns.exec("ip", &["link", "set", "lo", "up"])?;
    
    // Show IP addresses
    let addrs = ns.exec("ip", &["addr", "show"])?;
    println!("\nIP addresses:\n{}", addrs);
    
    println!("\nNamespace will be deleted on exit.");
    
    Ok(())
}
```

## Virtual Ethernet (veth) Pairs

Virtual Ethernet devices come in pairs and act like a virtual network cable. Packets transmitted on one end appear on the other end. These are crucial for connecting network namespaces to each other or to the host.

### C Example: Creating veth Pairs

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>

struct nl_req {
    struct nlmsghdr hdr;
    struct ifinfomsg info;
    char buf[1024];
};

// Add attribute to netlink message
static void add_attr(struct nlmsghdr *n, int maxlen, int type, 
                     const void *data, int datalen) {
    int len = RTA_LENGTH(datalen);
    struct rtattr *rta;
    
    if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
        fprintf(stderr, "Message too long\n");
        return;
    }
    
    rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = len;
    memcpy(RTA_DATA(rta), data, datalen);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
}

int create_veth_pair(const char *veth1, const char *veth2) {
    int sock;
    struct nl_req req;
    struct rtattr *linkinfo, *data, *peer_info;
    
    // Open netlink socket
    sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Initialize request
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    req.hdr.nlmsg_type = RTM_NEWLINK;
    req.info.ifi_family = AF_UNSPEC;
    
    // Add interface name
    add_attr(&req.hdr, sizeof(req), IFLA_IFNAME, veth1, strlen(veth1));
    
    // Add link info
    linkinfo = (struct rtattr *)(((char*)&req.hdr) + 
                                 NLMSG_ALIGN(req.hdr.nlmsg_len));
    add_attr(&req.hdr, sizeof(req), IFLA_LINKINFO, NULL, 0);
    
    add_attr(&req.hdr, sizeof(req), IFLA_INFO_KIND, "veth", 4);
    
    // Add peer info
    data = (struct rtattr *)(((char*)&req.hdr) + 
                             NLMSG_ALIGN(req.hdr.nlmsg_len));
    add_attr(&req.hdr, sizeof(req), IFLA_INFO_DATA, NULL, 0);
    
    peer_info = (struct rtattr *)(((char*)&req.hdr) + 
                                  NLMSG_ALIGN(req.hdr.nlmsg_len));
    add_attr(&req.hdr, sizeof(req), VETH_INFO_PEER, NULL, 0);
    
    struct ifinfomsg peer_ifi;
    memset(&peer_ifi, 0, sizeof(peer_ifi));
    peer_ifi.ifi_family = AF_UNSPEC;
    
    add_attr(&req.hdr, sizeof(req), IFLA_IFNAME, veth2, strlen(veth2));
    
    // Send request
    if (send(sock, &req, req.hdr.nlmsg_len, 0) < 0) {
        perror("send");
        close(sock);
        return -1;
    }
    
    printf("Created veth pair: %s <-> %s\n", veth1, veth2);
    
    close(sock);
    return 0;
}

int main() {
    if (geteuid() != 0) {
        fprintf(stderr, "This program requires root privileges\n");
        return 1;
    }
    
    // Create veth pair
    if (create_veth_pair("veth0", "veth1") == 0) {
        printf("Successfully created veth pair\n");
        printf("You can now assign these to different namespaces\n");
    }
    
    return 0;
}
```

### Rust Example: veth Management

```rust
use std::process::Command;
use std::io;

/// Represents a virtual ethernet pair
pub struct VethPair {
    pub end1: String,
    pub end2: String,
}

impl VethPair {
    /// Create a new veth pair
    pub fn create(name1: &str, name2: &str) -> io::Result<Self> {
        let output = Command::new("ip")
            .args(&["link", "add", name1, "type", "veth", "peer", "name", name2])
            .output()?;
        
        if !output.status.success() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Failed to create veth pair: {}", 
                       String::from_utf8_lossy(&output.stderr))
            ));
        }
        
        println!("Created veth pair: {} <-> {}", name1, name2);
        
        Ok(VethPair {
            end1: name1.to_string(),
            end2: name2.to_string(),
        })
    }
    
    /// Move one end to a network namespace
    pub fn move_to_namespace(&self, interface: &str, namespace: &str) -> io::Result<()> {
        let output = Command::new("ip")
            .args(&["link", "set", interface, "netns", namespace])
            .output()?;
        
        if output.status.success() {
            println!("Moved {} to namespace {}", interface, namespace);
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr).to_string()
            ))
        }
    }
    
    /// Set interface up
    pub fn set_up(&self, interface: &str, namespace: Option<&str>) -> io::Result<()> {
        let mut args = vec![];
        
        if let Some(ns) = namespace {
            args.extend_from_slice(&["netns", "exec", ns]);
        }
        
        args.extend_from_slice(&["ip", "link", "set", interface, "up"]);
        
        let output = Command::new(if namespace.is_some() { "ip" } else { "ip" })
            .args(&if namespace.is_some() { 
                args.clone() 
            } else { 
                vec!["link", "set", interface, "up"]
            })
            .output()?;
        
        if output.status.success() {
            println!("Set {} up", interface);
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr).to_string()
            ))
        }
    }
    
    /// Assign IP address to interface
    pub fn assign_ip(&self, interface: &str, ip_addr: &str, 
                     namespace: Option<&str>) -> io::Result<()> {
        let cmd = if let Some(ns) = namespace {
            Command::new("ip")
                .args(&["netns", "exec", ns, "ip", "addr", "add", 
                       ip_addr, "dev", interface])
                .output()?
        } else {
            Command::new("ip")
                .args(&["addr", "add", ip_addr, "dev", interface])
                .output()?
        };
        
        if cmd.status.success() {
            println!("Assigned {} to {}", ip_addr, interface);
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&cmd.stderr).to_string()
            ))
        }
    }
    
    /// Delete the veth pair
    pub fn delete(&self) -> io::Result<()> {
        let output = Command::new("ip")
            .args(&["link", "delete", &self.end1])
            .output()?;
        
        if output.status.success() {
            println!("Deleted veth pair");
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr).to_string()
            ))
        }
    }
}

fn main() -> io::Result<()> {
    println!("=== veth Pair Demo ===\n");
    
    // Create veth pair
    let veth = VethPair::create("veth0", "veth1")?;
    
    // Assign IP to veth0 (host side)
    veth.assign_ip("veth0", "10.0.0.1/24", None)?;
    veth.set_up("veth0", None)?;
    
    println!("\nveth pair created and configured");
    println!("veth0: 10.0.0.1/24 (host)");
    println!("veth1: ready to be moved to a namespace");
    
    // Cleanup
    println!("\nPress Enter to delete veth pair...");
    let mut input = String::new();
    std::io::stdin().read_line(&mut input)?;
    
    veth.delete()?;
    
    Ok(())
}
```

## Linux Bridge for Container Networking

A Linux bridge acts as a virtual switch, connecting multiple network interfaces. This is commonly used to connect containers to each other and to the external network.

### C Example: Bridge Communication

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>

#define BRIDGE_NAME "br0"
#define BUFFER_SIZE 1024

// Simple UDP server to demonstrate bridge connectivity
int run_container_server(const char* ip_addr, int port) {
    int sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip_addr);
    server_addr.sin_port = htons(port);
    
    // Bind socket
    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }
    
    printf("Container server listening on %s:%d\n", ip_addr, port);
    printf("Waiting for messages (Ctrl+C to stop)...\n");
    
    // Receive messages
    while (1) {
        ssize_t recv_len = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0,
                                   (struct sockaddr*)&client_addr, &client_len);
        
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            printf("Received from %s:%d: %s\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port),
                   buffer);
            
            // Echo back
            sendto(sock, buffer, recv_len, 0,
                  (struct sockaddr*)&client_addr, client_len);
        }
    }
    
    close(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip_address> <port>\n", argv[0]);
        fprintf(stderr, "Example: %s 10.0.0.2 8080\n", argv[0]);
        return 1;
    }
    
    return run_container_server(argv[1], atoi(argv[2]));
}
```

### Rust Example: Bridge and Container Connectivity

```rust
use std::net::UdpSocket;
use std::io::{self, Result};
use std::process::Command;

/// Container network bridge manager
pub struct Bridge {
    name: String,
    ip_range: String,
}

impl Bridge {
    /// Create a new bridge
    pub fn create(name: &str, ip_range: &str) -> Result<Self> {
        // Create bridge
        Command::new("ip")
            .args(&["link", "add", "name", name, "type", "bridge"])
            .output()?;
        
        // Assign IP to bridge
        Command::new("ip")
            .args(&["addr", "add", ip_range, "dev", name])
            .output()?;
        
        // Bring bridge up
        Command::new("ip")
            .args(&["link", "set", name, "up"])
            .output()?;
        
        println!("Created bridge {} with IP range {}", name, ip_range);
        
        Ok(Bridge {
            name: name.to_string(),
            ip_range: ip_range.to_string(),
        })
    }
    
    /// Attach an interface to the bridge
    pub fn attach_interface(&self, interface: &str) -> Result<()> {
        let output = Command::new("ip")
            .args(&["link", "set", interface, "master", &self.name])
            .output()?;
        
        if output.status.success() {
            println!("Attached {} to bridge {}", interface, self.name);
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr).to_string()
            ))
        }
    }
    
    /// Enable IP forwarding
    pub fn enable_forwarding() -> Result<()> {
        std::fs::write("/proc/sys/net/ipv4/ip_forward", "1")?;
        println!("Enabled IP forwarding");
        Ok(())
    }
    
    /// Setup NAT for containers
    pub fn setup_nat(&self, external_if: &str) -> Result<()> {
        // Add iptables rule for NAT
        Command::new("iptables")
            .args(&["-t", "nat", "-A", "POSTROUTING", 
                   "-s", &self.ip_range, "-o", external_if, 
                   "-j", "MASQUERADE"])
            .output()?;
        
        println!("Setup NAT for {} via {}", self.ip_range, external_if);
        Ok(())
    }
    
    /// Delete the bridge
    pub fn delete(&self) -> Result<()> {
        Command::new("ip")
            .args(&["link", "delete", &self.name])
            .output()?;
        
        println!("Deleted bridge {}", self.name);
        Ok(())
    }
}

/// Simple container network simulator
pub struct ContainerNetwork {
    bridge: Bridge,
    containers: Vec<String>,
}

impl ContainerNetwork {
    pub fn new(bridge_name: &str, subnet: &str) -> Result<Self> {
        let bridge = Bridge::create(bridge_name, subnet)?;
        Bridge::enable_forwarding()?;
        
        Ok(ContainerNetwork {
            bridge,
            containers: Vec::new(),
        })
    }
    
    pub fn add_container(&mut self, name: &str, ip: &str) -> Result<()> {
        // In a real implementation, this would:
        // 1. Create network namespace
        // 2. Create veth pair
        // 3. Attach one end to bridge
        // 4. Move other end to namespace
        // 5. Configure IP address
        
        self.containers.push(name.to_string());
        println!("Added container {} with IP {}", name, ip);
        Ok(())
    }
}

/// UDP echo server for testing container connectivity
pub fn run_udp_server(addr: &str) -> Result<()> {
    let socket = UdpSocket::bind(addr)?;
    println!("UDP server listening on {}", addr);
    println!("Waiting for messages...\n");
    
    let mut buf = [0u8; 1024];
    
    loop {
        match socket.recv_from(&mut buf) {
            Ok((size, src)) => {
                let msg = String::from_utf8_lossy(&buf[..size]);
                println!("Received from {}: {}", src, msg);
                
                // Echo back
                socket.send_to(&buf[..size], src)?;
                println!("Echoed back to {}\n", src);
            }
            Err(e) => {
                eprintln!("Error receiving: {}", e);
            }
        }
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() != 2 {
        eprintln!("Usage: {} <ip:port>", args[0]);
        eprintln!("Example: {} 10.0.0.1:8080", args[0]);
        return Ok(());
    }
    
    run_udp_server(&args[1])
}
```

## Overlay Networks (VXLAN)

Overlay networks create virtual networks on top of existing physical networks, enabling containers on different hosts to communicate as if they were on the same local network. VXLAN (Virtual Extensible LAN) is a popular overlay technology.

### C Example: VXLAN Basics

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// VXLAN header structure
struct vxlan_hdr {
    uint32_t vx_flags;  // Flags (8 bits) + Reserved (24 bits)
    uint32_t vx_vni;    // VXLAN Network Identifier (24 bits) + Reserved (8 bits)
};

#define VXLAN_PORT 4789
#define VXLAN_VNI_MASK 0xFFFFFF00

// Encapsulate packet in VXLAN
void vxlan_encapsulate(const unsigned char *payload, size_t payload_len,
                       uint32_t vni, unsigned char *output, size_t *output_len) {
    struct vxlan_hdr *vxlan = (struct vxlan_hdr *)output;
    
    // Set VXLAN flags (0x08 for valid VNI)
    vxlan->vx_flags = htonl(0x08000000);
    
    // Set VNI (24 bits, shifted left 8 bits)
    vxlan->vx_vni = htonl((vni << 8) & VXLAN_VNI_MASK);
    
    // Copy payload after VXLAN header
    memcpy(output + sizeof(struct vxlan_hdr), payload, payload_len);
    
    *output_len = sizeof(struct vxlan_hdr) + payload_len;
}

// Decapsulate VXLAN packet
int vxlan_decapsulate(const unsigned char *packet, size_t packet_len,
                     uint32_t *vni, unsigned char *payload, size_t *payload_len) {
    if (packet_len < sizeof(struct vxlan_hdr)) {
        return -1;
    }
    
    struct vxlan_hdr *vxlan = (struct vxlan_hdr *)packet;
    
    // Extract VNI
    *vni = (ntohl(vxlan->vx_vni) >> 8) & 0xFFFFFF;
    
    // Copy payload
    *payload_len = packet_len - sizeof(struct vxlan_hdr);
    memcpy(payload, packet + sizeof(struct vxlan_hdr), *payload_len);
    
    return 0;
}

int main() {
    // Example packet
    const char *message = "Hello from container!";
    unsigned char encap_packet[1500];
    unsigned char decap_payload[1500];
    size_t encap_len, decap_len;
    uint32_t vni = 42;  // VXLAN Network ID
    uint32_t extracted_vni;
    
    printf("=== VXLAN Encapsulation Demo ===\n\n");
    
    // Encapsulate
    vxlan_encapsulate((unsigned char *)message, strlen(message),
                     vni, encap_packet, &encap_len);
    
    printf("Original message: %s\n", message);
    printf("VXLAN VNI: %u\n", vni);
    printf("Encapsulated packet size: %zu bytes\n", encap_len);
    printf("VXLAN header size: %zu bytes\n\n", sizeof(struct vxlan_hdr));
    
    // Decapsulate
    if (vxlan_decapsulate(encap_packet, encap_len,
                         &extracted_vni, decap_payload, &decap_len) == 0) {
        decap_payload[decap_len] = '\0';
        printf("Decapsulated successfully\n");
        printf("Extracted VNI: %u\n", extracted_vni);
        printf("Payload: %s\n", decap_payload);
    }
    
    return 0;
}
```

### Rust Example: VXLAN Overlay Network

```rust
use std::net::{UdpSocket, SocketAddr, IpAddr};
use std::io::{self, Result, Error, ErrorKind};
use std::process::Command;

const VXLAN_PORT: u16 = 4789;
const VXLAN_FLAG_VALID_VNI: u32 = 0x08000000;

/// VXLAN header structure
#[repr(C)]
#[derive(Debug, Clone, Copy)]
struct VxlanHeader {
    flags: u32,  // 8 bits flags + 24 bits reserved
    vni: u32,    // 24 bits VNI + 8 bits reserved
}

impl VxlanHeader {
    fn new(vni: u32) -> Self {
        VxlanHeader {
            flags: VXLAN_FLAG_VALID_VNI.to_be(),
            vni: ((vni & 0xFFFFFF) << 8).to_be(),
        }
    }
    
    fn get_vni(&self) -> u32 {
        (u32::from_be(self.vni) >> 8) & 0xFFFFFF
    }
    
    fn to_bytes(&self) -> [u8; 8] {
        let mut bytes = [0u8; 8];
        bytes[0..4].copy_from_slice(&self.flags.to_ne_bytes());
        bytes[4..8].copy_from_slice(&self.vni.to_ne_bytes());
        bytes
    }
    
    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        if bytes.len() < 8 {
            return Err(Error::new(ErrorKind::InvalidData, "Invalid VXLAN header"));
        }
        
        let mut flags_bytes = [0u8; 4];
        let mut vni_bytes = [0u8; 4];
        
        flags_bytes.copy_from_slice(&bytes[0..4]);
        vni_bytes.copy_from_slice(&bytes[4..8]);
        
        Ok(VxlanHeader {
            flags: u32::from_ne_bytes(flags_bytes),
            vni: u32::from_ne_bytes(vni_bytes),
        })
    }
}

/// VXLAN tunnel endpoint
pub struct VxlanTunnel {
    vni: u32,
    local_addr: SocketAddr,
    remote_addr: SocketAddr,
    socket: UdpSocket,
}

impl VxlanTunnel {
    /// Create a new VXLAN tunnel
    pub fn new(vni: u32, local_ip: &str, remote_ip: &str) -> Result<Self> {
        let local_addr: SocketAddr = format!("{}:{}", local_ip, VXLAN_PORT).parse()
            .map_err(|e| Error::new(ErrorKind::InvalidInput, e))?;
        
        let remote_addr: SocketAddr = format!("{}:{}", remote_ip, VXLAN_PORT).parse()
            .map_err(|e| Error::new(ErrorKind::InvalidInput, e))?;
        
        let socket = UdpSocket::bind(local_addr)?;
        
        println!("Created VXLAN tunnel:");
        println!("  VNI: {}", vni);
        println!("  Local: {}", local_addr);
        println!("  Remote: {}", remote_addr);
        
        Ok(VxlanTunnel {
            vni,
            local_addr,
            remote_addr,
            socket,
        })
    }
    
    /// Encapsulate and send data
    pub fn send(&self, payload: &[u8]) -> Result<usize> {
        let header = VxlanHeader::new(self.vni);
        let header_bytes = header.to_bytes();
        
        // Combine header and payload
        let mut packet = Vec::with_capacity(header_bytes.len() + payload.len());
        packet.extend_from_slice(&header_bytes);
        packet.extend_from_slice(payload);
        
        let sent = self.socket.send_to(&packet, self.remote_addr)?;
        
        println!("Sent {} bytes (VNI: {})", sent, self.vni);
        Ok(sent)
    }
    
    /// Receive and decapsulate data
    pub fn receive(&self, buffer: &mut [u8]) -> Result<(usize, u32)> {
        let mut packet = vec![0u8; 65535];
        let (size, _src) = self.socket.recv_from(&mut packet)?;
        
        if size < 8 {
            return Err(Error::new(ErrorKind::InvalidData, "Packet too small"));
        }
        
        let header = VxlanHeader::from_bytes(&packet[..8])?;
        let vni = header.get_vni();
        
        let payload_size = size - 8;
        if payload_size > buffer.len() {
            return Err(Error::new(ErrorKind::InvalidData, "Buffer too small"));
        }
        
        buffer[..payload_size].copy_from_slice(&packet[8..size]);
        
        println!("Received {} bytes (VNI: {})", payload_size, vni);
        Ok((payload_size, vni))
    }
}

/// Create VXLAN interface using ip command
pub fn create_vxlan_interface(
    name: &str,
    vni: u32,
    local_ip: &str,
    remote_ip: &str,
    dev: &str
) -> Result<()> {
    let output = Command::new("ip")
        .args(&[
            "link", "add", name,
            "type", "vxlan",
            "id", &vni.to_string(),
            "local", local_ip,
            "remote", remote_ip,
            "dev", dev,
            "dstport", &VXLAN_PORT.to_string()
        ])
        .output()?;
    
    if !output.status.success() {
        return Err(Error::new(
            ErrorKind::Other,
            format!("Failed to create VXLAN interface: {}", 
                   String::from_utf8_lossy(&output.stderr))
        ));
    }
    
    println!("Created VXLAN interface {} (VNI: {})", name, vni);
    Ok(())
}

fn main() -> Result<()> {
    println!("=== VXLAN Overlay Network Demo ===\n");
    
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        println!("Usage:");
        println!("  {} send <local_ip> <remote_ip>", args[0]);
        println!("  {} receive <local_ip> <remote_ip>", args[0]);
        return Ok(());
    }
    
    match args[1].as_str() {
        "send" if args.len() == 4 => {
            let tunnel = VxlanTunnel::new(100, &args[2], &args[3])?;
            let message = b"Hello through VXLAN overlay!";
            tunnel.send(message)?;
            println!("Message sent: {}", String::from_utf8_lossy(message));
        }
        "receive" if args.len() == 4 => {
            let tunnel = VxlanTunnel::new(100, &args[2], &args[3])?;
            let mut buffer = [0u8; 1500];
            println!("Waiting for VXLAN packets...\n");
            
            loop {
                match tunnel.receive(&mut buffer) {
                    Ok((size, vni)) => {
                        let msg = String::from_utf8_lossy(&buffer[..size]);
                        println!("Received (VNI {}): {}\n", vni, msg);
                    }
                    Err(e) => eprintln!("Error: {}", e),
                }
            }
        }
        _ => {
            println!("Invalid arguments. Use 'send' or 'receive'");
        }
    }
    
    Ok(())
}
```

## Summary

Container networking relies on three core technologies that work together to provide isolated yet connected environments:

**Network Namespaces** provide complete network stack isolation, giving each container its own interfaces, routing tables, and firewall rules. This is fundamental to container security and multi-tenancy.

**Virtual Ethernet (veth) Pairs** act as virtual network cables, connecting namespaces together or to the host. One end typically stays on the host (often attached to a bridge), while the other moves into a container's namespace.

**Overlay Networks** like VXLAN enable containers across different physical hosts to communicate seamlessly. They encapsulate container traffic within UDP packets, creating virtual Layer 2 networks that span multiple physical servers. This is essential for distributed container orchestration platforms like Kubernetes and Docker Swarm.

Together, these technologies enable sophisticated network architectures: containers can be isolated from each other, connected via virtual switches (bridges), assigned IP addresses from private subnets, communicate across hosts via overlay networks, and access external networks through NAT or routing. The combination provides both the isolation needed for security and the connectivity required for modern microservices architectures.