# CAN Gateway and Routing

## Overview

A CAN Gateway acts as a bridge between multiple CAN networks, enabling communication across different network segments while providing message filtering, translation, and intelligent routing. This is essential in modern automotive and industrial systems where multiple CAN buses operate at different speeds or serve different functional domains.

## Core Concepts

### Gateway Functions

**Message Routing**: Selectively forwarding messages between CAN networks based on predefined rules, CAN IDs, or dynamic conditions.

**Protocol Translation**: Converting between different CAN standards (Classical CAN, CAN FD) or adjusting message formats between networks.

**Message Filtering**: Blocking unwanted traffic to prevent network congestion and maintain security boundaries between domains.

**Data Transformation**: Modifying message content during routing, such as scaling values or changing data representations.

**Traffic Management**: Load balancing, priority handling, and bandwidth optimization across multiple networks.

### Typical Use Cases

- **Automotive Domain Separation**: Isolating powertrain, chassis, body, and infotainment networks
- **Diagnostic Interfaces**: Providing controlled access to vehicle networks for diagnostic tools
- **Network Segmentation**: Reducing network load by limiting broadcast domains
- **Legacy Integration**: Connecting older CAN systems with modern CAN FD networks

## Programming Implementation

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#define MAX_ROUTES 256
#define MAX_CAN_INTERFACES 8

// Routing rule structure
typedef struct {
    uint32_t src_id;           // Source CAN ID
    uint32_t src_mask;         // Mask for ID matching
    int src_interface;         // Source interface index
    uint32_t dest_id;          // Destination CAN ID (for translation)
    int dest_interface;        // Destination interface index
    int enabled;               // Rule enabled flag
    uint64_t message_count;    // Statistics counter
} routing_rule_t;

// Gateway configuration
typedef struct {
    int can_sockets[MAX_CAN_INTERFACES];
    char interface_names[MAX_CAN_INTERFACES][16];
    int num_interfaces;
    routing_rule_t routes[MAX_ROUTES];
    int num_routes;
    pthread_mutex_t route_lock;
    int running;
} can_gateway_t;

// Initialize CAN socket
int init_can_socket(const char *interface) {
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;
    
    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    strcpy(ifr.ifr_name, interface);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }
    
    return sock;
}

// Add routing rule
int add_route(can_gateway_t *gateway, uint32_t src_id, uint32_t src_mask,
              int src_if, uint32_t dest_id, int dest_if) {
    pthread_mutex_lock(&gateway->route_lock);
    
    if (gateway->num_routes >= MAX_ROUTES) {
        pthread_mutex_unlock(&gateway->route_lock);
        return -1;
    }
    
    routing_rule_t *rule = &gateway->routes[gateway->num_routes];
    rule->src_id = src_id;
    rule->src_mask = src_mask;
    rule->src_interface = src_if;
    rule->dest_id = dest_id;
    rule->dest_interface = dest_if;
    rule->enabled = 1;
    rule->message_count = 0;
    
    gateway->num_routes++;
    
    pthread_mutex_unlock(&gateway->route_lock);
    return 0;
}

// Match and route message
int route_message(can_gateway_t *gateway, struct can_frame *frame, int src_if) {
    int routed = 0;
    
    pthread_mutex_lock(&gateway->route_lock);
    
    for (int i = 0; i < gateway->num_routes; i++) {
        routing_rule_t *rule = &gateway->routes[i];
        
        if (!rule->enabled || rule->src_interface != src_if) {
            continue;
        }
        
        // Check if message matches routing rule
        if ((frame->can_id & rule->src_mask) == (rule->src_id & rule->src_mask)) {
            struct can_frame routed_frame = *frame;
            
            // Translate CAN ID if needed
            if (rule->dest_id != 0) {
                routed_frame.can_id = rule->dest_id;
            }
            
            // Send to destination interface
            int dest_sock = gateway->can_sockets[rule->dest_interface];
            if (write(dest_sock, &routed_frame, sizeof(routed_frame)) != sizeof(routed_frame)) {
                perror("write");
            } else {
                rule->message_count++;
                routed = 1;
            }
        }
    }
    
    pthread_mutex_unlock(&gateway->route_lock);
    return routed;
}

// Gateway receive thread
void *gateway_thread(void *arg) {
    can_gateway_t *gateway = (can_gateway_t *)arg;
    fd_set readfds;
    int max_fd = 0;
    
    // Find maximum file descriptor
    for (int i = 0; i < gateway->num_interfaces; i++) {
        if (gateway->can_sockets[i] > max_fd) {
            max_fd = gateway->can_sockets[i];
        }
    }
    
    while (gateway->running) {
        FD_ZERO(&readfds);
        
        // Add all sockets to select set
        for (int i = 0; i < gateway->num_interfaces; i++) {
            FD_SET(gateway->can_sockets[i], &readfds);
        }
        
        struct timeval timeout = {1, 0}; // 1 second timeout
        int ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (ret < 0) {
            perror("select");
            break;
        }
        
        if (ret == 0) {
            continue; // Timeout
        }
        
        // Check which socket has data
        for (int i = 0; i < gateway->num_interfaces; i++) {
            if (FD_ISSET(gateway->can_sockets[i], &readfds)) {
                struct can_frame frame;
                int nbytes = read(gateway->can_sockets[i], &frame, sizeof(frame));
                
                if (nbytes == sizeof(frame)) {
                    // Route the message
                    route_message(gateway, &frame, i);
                }
            }
        }
    }
    
    return NULL;
}

// Initialize gateway
can_gateway_t *create_gateway(void) {
    can_gateway_t *gateway = calloc(1, sizeof(can_gateway_t));
    if (!gateway) return NULL;
    
    pthread_mutex_init(&gateway->route_lock, NULL);
    gateway->running = 1;
    
    return gateway;
}

// Add interface to gateway
int gateway_add_interface(can_gateway_t *gateway, const char *interface) {
    if (gateway->num_interfaces >= MAX_CAN_INTERFACES) {
        return -1;
    }
    
    int sock = init_can_socket(interface);
    if (sock < 0) {
        return -1;
    }
    
    int idx = gateway->num_interfaces;
    gateway->can_sockets[idx] = sock;
    strncpy(gateway->interface_names[idx], interface, 15);
    gateway->num_interfaces++;
    
    return idx;
}

// Print routing statistics
void print_statistics(can_gateway_t *gateway) {
    pthread_mutex_lock(&gateway->route_lock);
    
    printf("\n=== Gateway Routing Statistics ===\n");
    for (int i = 0; i < gateway->num_routes; i++) {
        routing_rule_t *rule = &gateway->routes[i];
        printf("Route %d: ID=0x%03X Mask=0x%03X %s->%s Count=%lu %s\n",
               i, rule->src_id, rule->src_mask,
               gateway->interface_names[rule->src_interface],
               gateway->interface_names[rule->dest_interface],
               rule->message_count,
               rule->enabled ? "ENABLED" : "DISABLED");
    }
    
    pthread_mutex_unlock(&gateway->route_lock);
}

// Example usage
int main(void) {
    can_gateway_t *gateway = create_gateway();
    if (!gateway) {
        fprintf(stderr, "Failed to create gateway\n");
        return 1;
    }
    
    // Add CAN interfaces
    int can0_idx = gateway_add_interface(gateway, "can0");
    int can1_idx = gateway_add_interface(gateway, "can1");
    int can2_idx = gateway_add_interface(gateway, "can2");
    
    if (can0_idx < 0 || can1_idx < 0 || can2_idx < 0) {
        fprintf(stderr, "Failed to initialize interfaces\n");
        return 1;
    }
    
    // Configure routing rules
    // Route engine data from can0 to can1
    add_route(gateway, 0x100, 0x7F0, can0_idx, 0, can1_idx);
    
    // Route diagnostic requests from can2 to can0
    add_route(gateway, 0x7DF, 0x7FF, can2_idx, 0, can0_idx);
    
    // Route responses from can0 back to can2
    add_route(gateway, 0x7E8, 0x7F8, can0_idx, 0, can2_idx);
    
    // Bidirectional routing for body control
    add_route(gateway, 0x200, 0x700, can0_idx, 0, can1_idx);
    add_route(gateway, 0x200, 0x700, can1_idx, 0, can0_idx);
    
    printf("Starting CAN Gateway...\n");
    printf("Interfaces: %s, %s, %s\n", 
           gateway->interface_names[can0_idx],
           gateway->interface_names[can1_idx],
           gateway->interface_names[can2_idx]);
    
    // Start gateway thread
    pthread_t thread;
    pthread_create(&thread, NULL, gateway_thread, gateway);
    
    // Run for 30 seconds and print statistics
    sleep(30);
    print_statistics(gateway);
    
    // Cleanup
    gateway->running = 0;
    pthread_join(thread, NULL);
    
    for (int i = 0; i < gateway->num_interfaces; i++) {
        close(gateway->can_sockets[i]);
    }
    
    pthread_mutex_destroy(&gateway->route_lock);
    free(gateway);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;
use socketcan::{CANSocket, CANFrame, CANFilter};

#[derive(Debug, Clone)]
struct RoutingRule {
    src_id: u32,
    src_mask: u32,
    src_interface: String,
    dest_id: Option<u32>,  // None means keep original ID
    dest_interface: String,
    enabled: bool,
    message_count: u64,
    last_message: Option<Instant>,
}

impl RoutingRule {
    fn matches(&self, frame: &CANFrame, interface: &str) -> bool {
        if !self.enabled || self.src_interface != interface {
            return false;
        }
        
        let frame_id = frame.id();
        (frame_id & self.src_mask) == (self.src_id & self.src_mask)
    }
    
    fn transform(&self, frame: &CANFrame) -> CANFrame {
        if let Some(new_id) = self.dest_id {
            CANFrame::new(new_id, frame.data(), false, false).unwrap()
        } else {
            frame.clone()
        }
    }
}

struct CANGateway {
    sockets: HashMap<String, CANSocket>,
    routes: Arc<Mutex<Vec<RoutingRule>>>,
    running: Arc<Mutex<bool>>,
}

impl CANGateway {
    fn new() -> Result<Self, Box<dyn std::error::Error>> {
        Ok(CANGateway {
            sockets: HashMap::new(),
            routes: Arc::new(Mutex::new(Vec::new())),
            running: Arc::new(Mutex::new(true)),
        })
    }
    
    fn add_interface(&mut self, interface: &str) -> Result<(), Box<dyn std::error::Error>> {
        let socket = CANSocket::open(interface)?;
        socket.set_read_timeout(Duration::from_millis(100))?;
        self.sockets.insert(interface.to_string(), socket);
        Ok(())
    }
    
    fn add_route(
        &mut self,
        src_id: u32,
        src_mask: u32,
        src_interface: &str,
        dest_id: Option<u32>,
        dest_interface: &str,
    ) {
        let mut routes = self.routes.lock().unwrap();
        routes.push(RoutingRule {
            src_id,
            src_mask,
            src_interface: src_interface.to_string(),
            dest_id,
            dest_interface: dest_interface.to_string(),
            enabled: true,
            message_count: 0,
            last_message: None,
        });
    }
    
    fn route_message(&self, frame: &CANFrame, src_interface: &str) -> usize {
        let mut routed_count = 0;
        let mut routes = self.routes.lock().unwrap();
        
        for rule in routes.iter_mut() {
            if rule.matches(frame, src_interface) {
                // Transform the frame if needed
                let routed_frame = rule.transform(frame);
                
                // Send to destination interface
                if let Some(dest_socket) = self.sockets.get(&rule.dest_interface) {
                    match dest_socket.write_frame(&routed_frame) {
                        Ok(_) => {
                            rule.message_count += 1;
                            rule.last_message = Some(Instant::now());
                            routed_count += 1;
                        }
                        Err(e) => {
                            eprintln!("Error routing to {}: {}", rule.dest_interface, e);
                        }
                    }
                }
            }
        }
        
        routed_count
    }
    
    fn start_routing_thread(&self, interface: String) -> thread::JoinHandle<()> {
        let socket = self.sockets.get(&interface).unwrap().try_clone().unwrap();
        let routes = Arc::clone(&self.routes);
        let running = Arc::clone(&self.running);
        let sockets_map = self.sockets.clone();
        
        thread::spawn(move || {
            println!("Started routing thread for {}", interface);
            
            while *running.lock().unwrap() {
                match socket.read_frame() {
                    Ok(frame) => {
                        // Route the message
                        let mut routed_count = 0;
                        let mut routes_locked = routes.lock().unwrap();
                        
                        for rule in routes_locked.iter_mut() {
                            if rule.matches(&frame, &interface) {
                                let routed_frame = rule.transform(&frame);
                                
                                if let Some(dest_socket) = sockets_map.get(&rule.dest_interface) {
                                    if let Ok(_) = dest_socket.write_frame(&routed_frame) {
                                        rule.message_count += 1;
                                        rule.last_message = Some(Instant::now());
                                        routed_count += 1;
                                    }
                                }
                            }
                        }
                        
                        if routed_count > 0 {
                            println!("Routed CAN ID 0x{:03X} from {} to {} destinations",
                                   frame.id(), interface, routed_count);
                        }
                    }
                    Err(e) => {
                        // Timeout is expected, other errors are not
                        if e.kind() != std::io::ErrorKind::WouldBlock {
                            eprintln!("Error reading from {}: {}", interface, e);
                        }
                    }
                }
            }
            
            println!("Stopped routing thread for {}", interface);
        })
    }
    
    fn print_statistics(&self) {
        let routes = self.routes.lock().unwrap();
        
        println!("\n=== Gateway Routing Statistics ===");
        for (idx, rule) in routes.iter().enumerate() {
            let last_msg = if let Some(instant) = rule.last_message {
                format!("{:.1}s ago", instant.elapsed().as_secs_f64())
            } else {
                "Never".to_string()
            };
            
            println!(
                "Route {}: ID=0x{:03X} Mask=0x{:03X} {}->{} Count={} Last={} {}",
                idx,
                rule.src_id,
                rule.src_mask,
                rule.src_interface,
                rule.dest_interface,
                rule.message_count,
                last_msg,
                if rule.enabled { "ENABLED" } else { "DISABLED" }
            );
        }
    }
    
    fn enable_route(&self, index: usize, enabled: bool) -> Result<(), String> {
        let mut routes = self.routes.lock().unwrap();
        if index >= routes.len() {
            return Err("Route index out of bounds".to_string());
        }
        routes[index].enabled = enabled;
        Ok(())
    }
    
    fn stop(&self) {
        *self.running.lock().unwrap() = false;
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut gateway = CANGateway::new()?;
    
    // Add CAN interfaces
    gateway.add_interface("can0")?;
    gateway.add_interface("can1")?;
    gateway.add_interface("can2")?;
    
    println!("CAN Gateway initialized with interfaces: can0, can1, can2");
    
    // Configure routing rules
    
    // Route powertrain data (0x100-0x17F) from can0 to can1
    gateway.add_route(0x100, 0xFF80, "can0", None, "can1");
    
    // Route diagnostic requests (0x7DF) from can2 to can0
    gateway.add_route(0x7DF, 0x7FF, "can2", None, "can0");
    
    // Route diagnostic responses (0x7E8-0x7EF) from can0 to can2
    gateway.add_route(0x7E8, 0x7F8, "can0", None, "can2");
    
    // Bidirectional routing for body control (0x200-0x2FF)
    gateway.add_route(0x200, 0xFF00, "can0", None, "can1");
    gateway.add_route(0x200, 0xFF00, "can1", None, "can0");
    
    // ID translation example: remap 0x300 from can0 to 0x400 on can1
    gateway.add_route(0x300, 0x7FF, "can0", Some(0x400), "can1");
    
    println!("\nRouting rules configured:");
    gateway.print_statistics();
    
    // Start routing threads for each interface
    let interfaces: Vec<String> = gateway.sockets.keys().cloned().collect();
    let mut threads = Vec::new();
    
    for interface in interfaces {
        let handle = gateway.start_routing_thread(interface);
        threads.push(handle);
    }
    
    println!("\nGateway running... Press Ctrl+C to stop");
    
    // Run for demonstration period
    thread::sleep(Duration::from_secs(30));
    
    // Print final statistics
    gateway.print_statistics();
    
    // Cleanup
    gateway.stop();
    for handle in threads {
        handle.join().unwrap();
    }
    
    println!("\nGateway stopped");
    
    Ok(())
}
```

## Summary

**CAN Gateway and Routing** provides critical infrastructure for modern multi-network CAN systems. Key capabilities include:

- **Message Routing**: Selective forwarding between networks based on CAN ID patterns and interface mappings
- **Protocol Translation**: Converting message formats, including ID remapping and data transformation
- **Traffic Management**: Filtering unwanted messages, preventing network congestion, and optimizing bandwidth
- **Security Boundaries**: Isolating network domains while providing controlled access for diagnostics and monitoring
- **Statistics and Monitoring**: Tracking message counts, routing performance, and network health

The C implementation demonstrates low-level socket programming with multi-threaded message handling and mutex-protected routing tables. The Rust implementation leverages type safety, thread-safe concurrency primitives, and the socketcan library for cleaner, more maintainable code.

CAN gateways are fundamental in automotive architectures where separate networks handle different functional domains (powertrain, chassis, body, infotainment), requiring intelligent bridging with security, performance, and reliability constraints. Modern gateways also support advanced features like message throttling, priority-based routing, and dynamic reconfiguration for flexible network topologies.