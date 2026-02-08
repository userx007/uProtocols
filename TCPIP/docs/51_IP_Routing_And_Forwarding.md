# IP Routing and Forwarding: Comprehensive Guide

## Detailed Overview

**IP Routing and Forwarding** is the fundamental process by which IP packets are moved from their source to their destination across interconnected networks. This involves making intelligent decisions about the path packets should take through the network infrastructure.

### Core Concepts

#### 1. **Routing vs. Forwarding**
- **Routing**: The process of building and maintaining routing tables that contain information about network topology and paths to destinations
- **Forwarding**: The actual process of moving packets from an input interface to an appropriate output interface based on routing table information

#### 2. **Routing Tables**
A routing table contains entries that specify:
- **Destination Network**: The target network or host
- **Netmask/Prefix Length**: Defines the network portion
- **Gateway/Next Hop**: The next router in the path
- **Interface**: The network interface to use
- **Metric**: Cost associated with the route
- **Flags**: Route characteristics (up, gateway, host, etc.)

#### 3. **Forwarding Decision Process**
```
1. Extract destination IP from packet
2. Perform longest prefix match in routing table
3. Determine next hop and output interface
4. Decrement TTL and recalculate checksum
5. Forward packet to next hop or deliver locally
```

#### 4. **Policy-Based Routing (PBR)**
Unlike traditional destination-based routing, PBR makes forwarding decisions based on:
- Source IP address
- Protocol type
- Port numbers
- Packet size
- Quality of Service (QoS) parameters
- Application type

---

## C/C++ Programming Examples

### Example 1: Reading the Linux Routing Table

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/rtnetlink.h>

#define BUFFER_SIZE 8192

// Structure to parse routing table entries
typedef struct {
    char destination[INET_ADDRSTRLEN];
    char gateway[INET_ADDRSTRLEN];
    char netmask[INET_ADDRSTRLEN];
    char interface[16];
    int metric;
    unsigned int flags;
} RouteEntry;

void print_route_entry(RouteEntry *entry) {
    printf("Destination: %-16s ", entry->destination);
    printf("Gateway: %-16s ", entry->gateway);
    printf("Netmask: %-16s ", entry->netmask);
    printf("Interface: %-8s ", entry->interface);
    printf("Metric: %d\n", entry->metric);
}

// Read routing table using /proc/net/route
int read_routing_table_proc() {
    FILE *fp;
    char line[256];
    char iface[16];
    unsigned long dest, gateway, mask;
    int flags, refcnt, use, metric, mtu, window, irtt;
    
    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        perror("Cannot open /proc/net/route");
        return -1;
    }
    
    // Skip header line
    fgets(line, sizeof(line), fp);
    
    printf("=== Routing Table ===\n");
    printf("Interface  Destination      Gateway          Netmask          Metric\n");
    printf("-----------------------------------------------------------------------\n");
    
    while (fgets(line, sizeof(line), fp)) {
        sscanf(line, "%s %lx %lx %x %d %d %d %lx %d %d %d",
               iface, &dest, &gateway, &flags, &refcnt, &use,
               &metric, &mask, &mtu, &window, &irtt);
        
        struct in_addr dest_addr, gw_addr, mask_addr;
        dest_addr.s_addr = dest;
        gw_addr.s_addr = gateway;
        mask_addr.s_addr = mask;
        
        printf("%-10s %-16s %-16s %-16s %d\n",
               iface,
               inet_ntoa(dest_addr),
               inet_ntoa(gw_addr),
               inet_ntoa(mask_addr),
               metric);
    }
    
    fclose(fp);
    return 0;
}
```

### Example 2: Route Lookup (Longest Prefix Match)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdint.h>

typedef struct RoutingEntry {
    uint32_t network;      // Network address
    uint32_t netmask;      // Network mask
    uint32_t gateway;      // Next hop gateway
    char interface[16];    // Output interface
    int metric;           // Route metric
    struct RoutingEntry *next;
} RoutingEntry;

typedef struct {
    RoutingEntry *head;
} RoutingTable;

// Initialize routing table
RoutingTable* create_routing_table() {
    RoutingTable *table = (RoutingTable*)malloc(sizeof(RoutingTable));
    table->head = NULL;
    return table;
}

// Add route to table
void add_route(RoutingTable *table, const char *network, const char *netmask,
               const char *gateway, const char *interface, int metric) {
    RoutingEntry *entry = (RoutingEntry*)malloc(sizeof(RoutingEntry));
    
    inet_pton(AF_INET, network, &entry->network);
    inet_pton(AF_INET, netmask, &entry->netmask);
    inet_pton(AF_INET, gateway, &entry->gateway);
    strncpy(entry->interface, interface, sizeof(entry->interface) - 1);
    entry->metric = metric;
    
    entry->next = table->head;
    table->head = entry;
}

// Count number of set bits in netmask (prefix length)
int count_mask_bits(uint32_t mask) {
    int count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

// Perform longest prefix match
RoutingEntry* route_lookup(RoutingTable *table, const char *dest_ip) {
    uint32_t dest;
    inet_pton(AF_INET, dest_ip, &dest);
    
    RoutingEntry *best_match = NULL;
    int longest_prefix = -1;
    
    RoutingEntry *current = table->head;
    while (current != NULL) {
        // Check if destination matches this route's network
        if ((dest & current->netmask) == (current->network & current->netmask)) {
            int prefix_len = count_mask_bits(ntohl(current->netmask));
            
            // Check if this is a longer match
            if (prefix_len > longest_prefix || 
                (prefix_len == longest_prefix && 
                 (best_match == NULL || current->metric < best_match->metric))) {
                longest_prefix = prefix_len;
                best_match = current;
            }
        }
        current = current->next;
    }
    
    return best_match;
}

void print_route(RoutingEntry *route) {
    if (route == NULL) {
        printf("No route found\n");
        return;
    }
    
    struct in_addr addr;
    char ip_str[INET_ADDRSTRLEN];
    
    addr.s_addr = route->gateway;
    inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
    
    printf("Gateway: %s, Interface: %s, Metric: %d\n",
           ip_str, route->interface, route->metric);
}

int main() {
    RoutingTable *table = create_routing_table();
    
    // Add some routes
    add_route(table, "0.0.0.0", "0.0.0.0", "192.168.1.1", "eth0", 100);
    add_route(table, "192.168.1.0", "255.255.255.0", "0.0.0.0", "eth0", 0);
    add_route(table, "10.0.0.0", "255.0.0.0", "192.168.1.254", "eth0", 50);
    add_route(table, "172.16.0.0", "255.240.0.0", "192.168.1.253", "eth1", 75);
    
    // Test route lookups
    printf("Looking up route for 192.168.1.100:\n");
    print_route(route_lookup(table, "192.168.1.100"));
    
    printf("\nLooking up route for 10.5.10.20:\n");
    print_route(route_lookup(table, "10.5.10.20"));
    
    printf("\nLooking up route for 8.8.8.8:\n");
    print_route(route_lookup(table, "8.8.8.8"));
    
    return 0;
}
```

### Example 3: Adding Routes with Netlink (Linux)

```cpp
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>
#include <unistd.h>

class RouteManager {
private:
    int sock;
    
    struct {
        struct nlmsghdr n;
        struct rtmsg r;
        char buf[4096];
    } req;
    
public:
    RouteManager() {
        sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (sock < 0) {
            throw std::runtime_error("Failed to create netlink socket");
        }
    }
    
    ~RouteManager() {
        if (sock >= 0) close(sock);
    }
    
    bool add_route(const std::string& dest, int prefix_len,
                   const std::string& gateway, const std::string& device) {
        memset(&req, 0, sizeof(req));
        
        // Setup netlink message header
        req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
        req.n.nlmsg_type = RTM_NEWROUTE;
        
        // Setup route message
        req.r.rtm_family = AF_INET;
        req.r.rtm_table = RT_TABLE_MAIN;
        req.r.rtm_protocol = RTPROT_BOOT;
        req.r.rtm_scope = RT_SCOPE_UNIVERSE;
        req.r.rtm_type = RTN_UNICAST;
        req.r.rtm_dst_len = prefix_len;
        
        // Add destination attribute
        struct in_addr dst_addr;
        inet_pton(AF_INET, dest.c_str(), &dst_addr);
        add_rtattr(&req.n, sizeof(req), RTA_DST, &dst_addr, sizeof(dst_addr));
        
        // Add gateway attribute
        struct in_addr gw_addr;
        inet_pton(AF_INET, gateway.c_str(), &gw_addr);
        add_rtattr(&req.n, sizeof(req), RTA_GATEWAY, &gw_addr, sizeof(gw_addr));
        
        // Send message
        if (send(sock, &req, req.n.nlmsg_len, 0) < 0) {
            perror("Failed to send netlink message");
            return false;
        }
        
        return true;
    }
    
    bool delete_route(const std::string& dest, int prefix_len) {
        memset(&req, 0, sizeof(req));
        
        req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        req.n.nlmsg_flags = NLM_F_REQUEST;
        req.n.nlmsg_type = RTM_DELROUTE;
        
        req.r.rtm_family = AF_INET;
        req.r.rtm_table = RT_TABLE_MAIN;
        req.r.rtm_dst_len = prefix_len;
        
        struct in_addr dst_addr;
        inet_pton(AF_INET, dest.c_str(), &dst_addr);
        add_rtattr(&req.n, sizeof(req), RTA_DST, &dst_addr, sizeof(dst_addr));
        
        if (send(sock, &req, req.n.nlmsg_len, 0) < 0) {
            perror("Failed to send netlink message");
            return false;
        }
        
        return true;
    }
    
private:
    void add_rtattr(struct nlmsghdr *n, int maxlen, int type,
                    const void *data, int alen) {
        int len = RTA_LENGTH(alen);
        struct rtattr *rta;
        
        if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
            throw std::runtime_error("Message exceeded maximum length");
        }
        
        rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
        rta->rta_type = type;
        rta->rta_len = len;
        memcpy(RTA_DATA(rta), data, alen);
        n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
    }
};

int main() {
    try {
        RouteManager rm;
        
        // Add a route: 10.0.0.0/8 via 192.168.1.1
        if (rm.add_route("10.0.0.0", 8, "192.168.1.1", "eth0")) {
            std::cout << "Route added successfully\n";
        }
        
        // Delete the route
        if (rm.delete_route("10.0.0.0", 8)) {
            std::cout << "Route deleted successfully\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

---

## Rust Programming Examples

### Example 1: Reading Routing Table

```rust
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::net::Ipv4Addr;

#[derive(Debug)]
struct RouteEntry {
    interface: String,
    destination: Ipv4Addr,
    gateway: Ipv4Addr,
    flags: u32,
    metric: u32,
    mask: Ipv4Addr,
}

impl RouteEntry {
    fn from_proc_line(line: &str) -> Option<RouteEntry> {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 11 {
            return None;
        }

        let interface = parts[0].to_string();
        let destination = parse_hex_ip(parts[1])?;
        let gateway = parse_hex_ip(parts[2])?;
        let flags = u32::from_str_radix(parts[3], 16).ok()?;
        let metric = parts[6].parse().ok()?;
        let mask = parse_hex_ip(parts[7])?;

        Some(RouteEntry {
            interface,
            destination,
            gateway,
            flags,
            metric,
            mask,
        })
    }
}

fn parse_hex_ip(hex: &str) -> Option<Ipv4Addr> {
    let val = u32::from_str_radix(hex, 16).ok()?;
    Some(Ipv4Addr::from(val.to_le_bytes()))
}

fn read_routing_table() -> std::io::Result<Vec<RouteEntry>> {
    let file = File::open("/proc/net/route")?;
    let reader = BufReader::new(file);
    let mut routes = Vec::new();

    for (i, line) in reader.lines().enumerate() {
        if i == 0 {
            continue; // Skip header
        }

        if let Ok(line_str) = line {
            if let Some(entry) = RouteEntry::from_proc_line(&line_str) {
                routes.push(entry);
            }
        }
    }

    Ok(routes)
}

fn main() -> std::io::Result<()> {
    println!("=== Routing Table ===\n");
    println!("{:<10} {:<16} {:<16} {:<16} {:<8}", 
             "Interface", "Destination", "Gateway", "Netmask", "Metric");
    println!("{}", "-".repeat(80));

    let routes = read_routing_table()?;
    for route in routes {
        println!("{:<10} {:<16} {:<16} {:<16} {:<8}",
                 route.interface,
                 route.destination,
                 route.gateway,
                 route.mask,
                 route.metric);
    }

    Ok(())
}
```

### Example 2: Route Lookup with Trie (Efficient)

```rust
use std::net::Ipv4Addr;
use std::collections::HashMap;

#[derive(Debug, Clone)]
struct RouteInfo {
    gateway: Ipv4Addr,
    interface: String,
    metric: u32,
}

struct TrieNode {
    route: Option<RouteInfo>,
    children: [Option<Box<TrieNode>>; 2],
}

impl TrieNode {
    fn new() -> Self {
        TrieNode {
            route: None,
            children: [None, None],
        }
    }
}

struct RoutingTable {
    root: TrieNode,
}

impl RoutingTable {
    fn new() -> Self {
        RoutingTable {
            root: TrieNode::new(),
        }
    }

    fn add_route(&mut self, network: Ipv4Addr, prefix_len: u8, route: RouteInfo) {
        let ip_bits = u32::from(network);
        let mut node = &mut self.root;

        for i in 0..prefix_len {
            let bit = ((ip_bits >> (31 - i)) & 1) as usize;
            node = node.children[bit]
                .get_or_insert_with(|| Box::new(TrieNode::new()));
        }

        node.route = Some(route);
    }

    fn lookup(&self, dest: Ipv4Addr) -> Option<&RouteInfo> {
        let ip_bits = u32::from(dest);
        let mut node = &self.root;
        let mut best_match: Option<&RouteInfo> = None;

        for i in 0..32 {
            // Update best match if current node has a route
            if let Some(ref route) = node.route {
                best_match = Some(route);
            }

            let bit = ((ip_bits >> (31 - i)) & 1) as usize;
            match &node.children[bit] {
                Some(child) => node = child,
                None => break,
            }
        }

        // Check final node
        if let Some(ref route) = node.route {
            best_match = Some(route);
        }

        best_match
    }

    fn print_table(&self) {
        self.print_node(&self.root, 0, 0, 0);
    }

    fn print_node(&self, node: &TrieNode, depth: usize, prefix: u32, prefix_len: u8) {
        if let Some(ref route) = node.route {
            let ip = Ipv4Addr::from(prefix << (32 - prefix_len));
            println!("{}/{} -> gateway: {}, interface: {}, metric: {}",
                     ip, prefix_len, route.gateway, route.interface, route.metric);
        }

        for (bit, child) in node.children.iter().enumerate() {
            if let Some(child_node) = child {
                let new_prefix = prefix | ((bit as u32) << (31 - prefix_len));
                self.print_node(child_node, depth + 1, new_prefix, prefix_len + 1);
            }
        }
    }
}

fn main() {
    let mut table = RoutingTable::new();

    // Add routes
    table.add_route(
        "0.0.0.0".parse().unwrap(),
        0,
        RouteInfo {
            gateway: "192.168.1.1".parse().unwrap(),
            interface: "eth0".to_string(),
            metric: 100,
        },
    );

    table.add_route(
        "192.168.1.0".parse().unwrap(),
        24,
        RouteInfo {
            gateway: "0.0.0.0".parse().unwrap(),
            interface: "eth0".to_string(),
            metric: 0,
        },
    );

    table.add_route(
        "10.0.0.0".parse().unwrap(),
        8,
        RouteInfo {
            gateway: "192.168.1.254".parse().unwrap(),
            interface: "eth0".to_string(),
            metric: 50,
        },
    );

    println!("=== Routing Table ===");
    table.print_table();

    // Test lookups
    println!("\n=== Route Lookups ===");
    let test_ips = vec![
        "192.168.1.100",
        "10.5.10.20",
        "8.8.8.8",
        "172.16.0.1",
    ];

    for ip_str in test_ips {
        let ip: Ipv4Addr = ip_str.parse().unwrap();
        print!("Route for {}: ", ip);
        match table.lookup(ip) {
            Some(route) => {
                println!("gateway={}, interface={}, metric={}",
                         route.gateway, route.interface, route.metric);
            }
            None => println!("No route found"),
        }
    }
}
```

### Example 3: Policy-Based Routing

```rust
use std::net::Ipv4Addr;

#[derive(Debug, Clone, PartialEq)]
enum Protocol {
    TCP,
    UDP,
    ICMP,
    Any,
}

#[derive(Debug, Clone)]
struct PacketInfo {
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    protocol: Protocol,
    src_port: Option<u16>,
    dst_port: Option<u16>,
    tos: u8, // Type of Service
}

#[derive(Debug, Clone)]
struct PolicyRule {
    priority: u32,
    src_network: Option<(Ipv4Addr, u8)>,
    dst_network: Option<(Ipv4Addr, u8)>,
    protocol: Option<Protocol>,
    port_range: Option<(u16, u16)>,
    action: RoutingAction,
}

#[derive(Debug, Clone)]
enum RoutingAction {
    UseTable(String),
    UseGateway(Ipv4Addr),
    Drop,
    UseInterface(String),
}

struct PolicyBasedRouter {
    rules: Vec<PolicyRule>,
}

impl PolicyBasedRouter {
    fn new() -> Self {
        PolicyBasedRouter { rules: Vec::new() }
    }

    fn add_rule(&mut self, rule: PolicyRule) {
        // Insert rule in priority order
        let pos = self.rules.iter()
            .position(|r| r.priority > rule.priority)
            .unwrap_or(self.rules.len());
        self.rules.insert(pos, rule);
    }

    fn match_network(ip: Ipv4Addr, network: &(Ipv4Addr, u8)) -> bool {
        let (net_addr, prefix_len) = network;
        let mask = !0u32 << (32 - prefix_len);
        let ip_u32 = u32::from(ip);
        let net_u32 = u32::from(*net_addr);
        (ip_u32 & mask) == (net_u32 & mask)
    }

    fn match_rule(&self, rule: &PolicyRule, packet: &PacketInfo) -> bool {
        // Check source network
        if let Some(ref src_net) = rule.src_network {
            if !Self::match_network(packet.src_ip, src_net) {
                return false;
            }
        }

        // Check destination network
        if let Some(ref dst_net) = rule.dst_network {
            if !Self::match_network(packet.dst_ip, dst_net) {
                return false;
            }
        }

        // Check protocol
        if let Some(ref proto) = rule.protocol {
            if *proto != Protocol::Any && *proto != packet.protocol {
                return false;
            }
        }

        // Check port range
        if let Some((min_port, max_port)) = rule.port_range {
            if let Some(dst_port) = packet.dst_port {
                if dst_port < min_port || dst_port > max_port {
                    return false;
                }
            } else {
                return false;
            }
        }

        true
    }

    fn route_packet(&self, packet: &PacketInfo) -> Option<&RoutingAction> {
        for rule in &self.rules {
            if self.match_rule(rule, packet) {
                return Some(&rule.action);
            }
        }
        None
    }
}

fn main() {
    let mut pbr = PolicyBasedRouter::new();

    // Rule 1: High priority - Route HTTP traffic through specific gateway
    pbr.add_rule(PolicyRule {
        priority: 10,
        src_network: None,
        dst_network: None,
        protocol: Some(Protocol::TCP),
        port_range: Some((80, 80)),
        action: RoutingAction::UseGateway("192.168.1.10".parse().unwrap()),
    });

    // Rule 2: Route internal traffic directly
    pbr.add_rule(PolicyRule {
        priority: 20,
        src_network: Some(("192.168.1.0".parse().unwrap(), 24)),
        dst_network: Some(("192.168.1.0".parse().unwrap(), 24)),
        protocol: None,
        port_range: None,
        action: RoutingAction::UseInterface("eth0".to_string()),
    });

    // Rule 3: Route traffic from specific subnet via VPN
    pbr.add_rule(PolicyRule {
        priority: 30,
        src_network: Some(("10.0.0.0".parse().unwrap(), 8)),
        dst_network: None,
        protocol: None,
        port_range: None,
        action: RoutingAction::UseInterface("tun0".to_string()),
    });

    // Rule 4: Default - use main routing table
    pbr.add_rule(PolicyRule {
        priority: 100,
        src_network: None,
        dst_network: None,
        protocol: None,
        port_range: None,
        action: RoutingAction::UseTable("main".to_string()),
    });

    // Test packets
    let test_packets = vec![
        PacketInfo {
            src_ip: "192.168.1.50".parse().unwrap(),
            dst_ip: "8.8.8.8".parse().unwrap(),
            protocol: Protocol::TCP,
            src_port: Some(50000),
            dst_port: Some(80),
            tos: 0,
        },
        PacketInfo {
            src_ip: "10.0.5.10".parse().unwrap(),
            dst_ip: "172.16.0.1".parse().unwrap(),
            protocol: Protocol::UDP,
            src_port: Some(12345),
            dst_port: Some(53),
            tos: 0,
        },
        PacketInfo {
            src_ip: "192.168.1.50".parse().unwrap(),
            dst_ip: "192.168.1.100".parse().unwrap(),
            protocol: Protocol::ICMP,
            src_port: None,
            dst_port: None,
            tos: 0,
        },
    ];

    println!("=== Policy-Based Routing Decisions ===\n");
    for (i, packet) in test_packets.iter().enumerate() {
        println!("Packet {}:", i + 1);
        println!("  {}:{:?} -> {}:{:?} [{}]",
                 packet.src_ip, packet.src_port,
                 packet.dst_ip, packet.dst_port,
                 format!("{:?}", packet.protocol));
        
        match pbr.route_packet(packet) {
            Some(action) => println!("  Action: {:?}\n", action),
            None => println!("  Action: No matching rule (drop)\n"),
        }
    }
}
```

---

## Summary

**IP Routing and Forwarding** is the cornerstone of internetworking that enables packets to traverse complex network topologies from source to destination:

### Key Takeaways:

1. **Routing vs Forwarding**: Routing builds the map (routing table), forwarding uses it to move packets

2. **Longest Prefix Match**: The fundamental algorithm for route lookup - more specific routes (longer prefixes) take precedence over general ones

3. **Routing Table Components**: Each entry contains destination network, netmask, next-hop gateway, outgoing interface, and metric

4. **Forwarding Process**: Extract destination IP → Lookup route → Select best match → Decrement TTL → Forward packet

5. **Policy-Based Routing**: Extends traditional destination-based routing by considering source addresses, protocols, ports, and QoS parameters for more sophisticated traffic engineering

6. **Implementation Approaches**:
   - **Simple linear search**: Easy to implement, O(n) lookup time
   - **Trie-based**: Efficient O(k) lookup where k is address length, ideal for production routers
   - **Hash tables**: Fast for exact matches but challenging for prefix matching

7. **Programming Considerations**:
   - **C/C++**: Direct system access via netlink, ioctl, or /proc filesystem
   - **Rust**: Strong type safety with efficient data structures, great for networking code
   - Both languages can achieve high-performance routing implementations

8. **Real-world Applications**: Software routers, SDN controllers, load balancers, firewalls, and VPN gateways all rely on these routing and forwarding principles

This topic is fundamental for understanding how the Internet works and is essential knowledge for network engineers, systems programmers, and anyone building networked applications.