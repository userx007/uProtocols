# Load Balancing Strategies

Load balancing is a critical technique for distributing network traffic across multiple servers to optimize resource utilization, maximize throughput, minimize response time, and avoid overloading any single server. Here's a comprehensive look at the three primary strategies:

## Overview

**Load balancing** sits between clients and backend servers, intercepting incoming requests and distributing them according to specific algorithms. The goal is to ensure no single server bears too much load while maintaining high availability and reliability.

### Key Strategies

**1. Round-Robin**
Distributes requests sequentially to each server in a circular order. Simple and effective when all servers have similar capacity and requests require similar processing time.

**2. Least Connections**
Routes new requests to the server currently handling the fewest active connections. Better for scenarios where connection duration varies significantly.

**3. Consistent Hashing**
Maps both requests (by client IP, session ID, etc.) and servers to points on a hash ring. Ensures the same client typically connects to the same server, minimizing cache misses when servers are added or removed.

---

## C Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_SERVERS 10
#define HASH_RING_SIZE 360

// Server structure
typedef struct {
    char ip[16];
    int port;
    int active_connections;
    int is_active;
} Server;

// Load balancer structure
typedef struct {
    Server servers[MAX_SERVERS];
    int server_count;
    int current_index; // For round-robin
    uint32_t hash_ring[HASH_RING_SIZE]; // For consistent hashing
} LoadBalancer;

// Initialize load balancer
void lb_init(LoadBalancer *lb) {
    lb->server_count = 0;
    lb->current_index = 0;
    memset(lb->hash_ring, 0, sizeof(lb->hash_ring));
}

// Add server
void lb_add_server(LoadBalancer *lb, const char *ip, int port) {
    if (lb->server_count >= MAX_SERVERS) return;
    
    Server *srv = &lb->servers[lb->server_count];
    strncpy(srv->ip, ip, 15);
    srv->ip[15] = '\0';
    srv->port = port;
    srv->active_connections = 0;
    srv->is_active = 1;
    lb->server_count++;
}

// STRATEGY 1: Round-Robin
Server* lb_round_robin(LoadBalancer *lb) {
    if (lb->server_count == 0) return NULL;
    
    int start = lb->current_index;
    do {
        Server *srv = &lb->servers[lb->current_index];
        lb->current_index = (lb->current_index + 1) % lb->server_count;
        
        if (srv->is_active) {
            return srv;
        }
    } while (lb->current_index != start);
    
    return NULL; // No active servers
}

// STRATEGY 2: Least Connections
Server* lb_least_connections(LoadBalancer *lb) {
    if (lb->server_count == 0) return NULL;
    
    Server *selected = NULL;
    int min_connections = INT32_MAX;
    
    for (int i = 0; i < lb->server_count; i++) {
        Server *srv = &lb->servers[i];
        if (srv->is_active && srv->active_connections < min_connections) {
            min_connections = srv->active_connections;
            selected = srv;
        }
    }
    
    return selected;
}

// Simple hash function (djb2)
uint32_t hash_function(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// STRATEGY 3: Consistent Hashing - Build ring
void lb_build_hash_ring(LoadBalancer *lb) {
    // Clear ring
    memset(lb->hash_ring, 0xFF, sizeof(lb->hash_ring)); // 0xFFFFFFFF = no server
    
    // Add each server to multiple positions (virtual nodes)
    int virtual_nodes = HASH_RING_SIZE / lb->server_count;
    
    for (int i = 0; i < lb->server_count; i++) {
        if (!lb->servers[i].is_active) continue;
        
        for (int v = 0; v < virtual_nodes; v++) {
            char vnode[64];
            snprintf(vnode, sizeof(vnode), "%s:%d#%d", 
                    lb->servers[i].ip, lb->servers[i].port, v);
            uint32_t hash = hash_function(vnode) % HASH_RING_SIZE;
            lb->hash_ring[hash] = i;
        }
    }
}

// Consistent Hashing - Get server
Server* lb_consistent_hash(LoadBalancer *lb, const char *key) {
    if (lb->server_count == 0) return NULL;
    
    uint32_t hash = hash_function(key) % HASH_RING_SIZE;
    
    // Find next server on ring
    for (int i = 0; i < HASH_RING_SIZE; i++) {
        uint32_t pos = (hash + i) % HASH_RING_SIZE;
        if (lb->hash_ring[pos] != 0xFFFFFFFF) {
            return &lb->servers[lb->hash_ring[pos]];
        }
    }
    
    return NULL;
}

// Example usage
int main() {
    LoadBalancer lb;
    lb_init(&lb);
    
    // Add servers
    lb_add_server(&lb, "192.168.1.10", 8080);
    lb_add_server(&lb, "192.168.1.11", 8080);
    lb_add_server(&lb, "192.168.1.12", 8080);
    
    printf("=== Round-Robin ===\n");
    for (int i = 0; i < 6; i++) {
        Server *srv = lb_round_robin(&lb);
        printf("Request %d -> %s:%d\n", i, srv->ip, srv->port);
    }
    
    printf("\n=== Least Connections ===\n");
    lb.servers[0].active_connections = 5;
    lb.servers[1].active_connections = 2;
    lb.servers[2].active_connections = 8;
    
    for (int i = 0; i < 4; i++) {
        Server *srv = lb_least_connections(&lb);
        printf("Request %d -> %s:%d (connections: %d)\n", 
               i, srv->ip, srv->port, srv->active_connections);
        srv->active_connections++; // Simulate new connection
    }
    
    printf("\n=== Consistent Hashing ===\n");
    lb_build_hash_ring(&lb);
    
    const char *clients[] = {"client-A", "client-B", "client-C", 
                            "client-A", "client-D", "client-B"};
    for (int i = 0; i < 6; i++) {
        Server *srv = lb_consistent_hash(&lb, clients[i]);
        printf("%s -> %s:%d\n", clients[i], srv->ip, srv->port);
    }
    
    return 0;
}
```

---

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <memory>
#include <functional>

class Server {
public:
    std::string ip;
    int port;
    int active_connections;
    bool is_active;
    
    Server(const std::string& ip, int port) 
        : ip(ip), port(port), active_connections(0), is_active(true) {}
    
    std::string address() const {
        return ip + ":" + std::to_string(port);
    }
};

class LoadBalancer {
private:
    std::vector<std::shared_ptr<Server>> servers;
    size_t round_robin_index;
    std::map<size_t, std::shared_ptr<Server>> hash_ring;
    
    // Hash function
    size_t hash(const std::string& key) const {
        return std::hash<std::string>{}(key);
    }
    
public:
    LoadBalancer() : round_robin_index(0) {}
    
    void add_server(const std::string& ip, int port) {
        servers.push_back(std::make_shared<Server>(ip, port));
    }
    
    // STRATEGY 1: Round-Robin
    std::shared_ptr<Server> round_robin() {
        if (servers.empty()) return nullptr;
        
        size_t start = round_robin_index;
        do {
            auto& srv = servers[round_robin_index];
            round_robin_index = (round_robin_index + 1) % servers.size();
            
            if (srv->is_active) {
                return srv;
            }
        } while (round_robin_index != start);
        
        return nullptr;
    }
    
    // STRATEGY 2: Least Connections
    std::shared_ptr<Server> least_connections() {
        if (servers.empty()) return nullptr;
        
        auto min_it = std::min_element(
            servers.begin(), 
            servers.end(),
            [](const auto& a, const auto& b) {
                if (!a->is_active) return false;
                if (!b->is_active) return true;
                return a->active_connections < b->active_connections;
            }
        );
        
        return (min_it != servers.end() && (*min_it)->is_active) 
               ? *min_it : nullptr;
    }
    
    // STRATEGY 3: Consistent Hashing - Build ring
    void build_hash_ring(int virtual_nodes = 150) {
        hash_ring.clear();
        
        for (auto& srv : servers) {
            if (!srv->is_active) continue;
            
            for (int i = 0; i < virtual_nodes; i++) {
                std::string vnode = srv->address() + "#" + std::to_string(i);
                size_t hash_val = hash(vnode);
                hash_ring[hash_val] = srv;
            }
        }
    }
    
    // Consistent Hashing - Get server
    std::shared_ptr<Server> consistent_hash(const std::string& key) {
        if (hash_ring.empty()) return nullptr;
        
        size_t hash_val = hash(key);
        
        // Find first server with hash >= key's hash
        auto it = hash_ring.lower_bound(hash_val);
        
        // Wrap around if we've gone past the end
        if (it == hash_ring.end()) {
            it = hash_ring.begin();
        }
        
        return it->second;
    }
    
    const std::vector<std::shared_ptr<Server>>& get_servers() const {
        return servers;
    }
};

int main() {
    LoadBalancer lb;
    
    lb.add_server("192.168.1.10", 8080);
    lb.add_server("192.168.1.11", 8080);
    lb.add_server("192.168.1.12", 8080);
    
    std::cout << "=== Round-Robin ===" << std::endl;
    for (int i = 0; i < 6; i++) {
        auto srv = lb.round_robin();
        std::cout << "Request " << i << " -> " << srv->address() << std::endl;
    }
    
    std::cout << "\n=== Least Connections ===" << std::endl;
    lb.get_servers()[0]->active_connections = 5;
    lb.get_servers()[1]->active_connections = 2;
    lb.get_servers()[2]->active_connections = 8;
    
    for (int i = 0; i < 4; i++) {
        auto srv = lb.least_connections();
        std::cout << "Request " << i << " -> " << srv->address() 
                  << " (connections: " << srv->active_connections << ")" 
                  << std::endl;
        srv->active_connections++;
    }
    
    std::cout << "\n=== Consistent Hashing ===" << std::endl;
    lb.build_hash_ring();
    
    std::vector<std::string> clients = {
        "client-A", "client-B", "client-C", 
        "client-A", "client-D", "client-B"
    };
    
    for (const auto& client : clients) {
        auto srv = lb.consistent_hash(client);
        std::cout << client << " -> " << srv->address() << std::endl;
    }
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::collections::hash_map::DefaultHasher;

#[derive(Clone)]
struct Server {
    ip: String,
    port: u16,
    active_connections: usize,
    is_active: bool,
}

impl Server {
    fn new(ip: String, port: u16) -> Self {
        Server {
            ip,
            port,
            active_connections: 0,
            is_active: true,
        }
    }
    
    fn address(&self) -> String {
        format!("{}:{}", self.ip, self.port)
    }
}

struct LoadBalancer {
    servers: Vec<Server>,
    round_robin_index: usize,
    hash_ring: HashMap<u64, usize>, // hash -> server index
}

impl LoadBalancer {
    fn new() -> Self {
        LoadBalancer {
            servers: Vec::new(),
            round_robin_index: 0,
            hash_ring: HashMap::new(),
        }
    }
    
    fn add_server(&mut self, ip: String, port: u16) {
        self.servers.push(Server::new(ip, port));
    }
    
    // Hash function
    fn hash<T: Hash>(&self, item: T) -> u64 {
        let mut hasher = DefaultHasher::new();
        item.hash(&mut hasher);
        hasher.finish()
    }
    
    // STRATEGY 1: Round-Robin
    fn round_robin(&mut self) -> Option<&mut Server> {
        if self.servers.is_empty() {
            return None;
        }
        
        let start = self.round_robin_index;
        loop {
            let index = self.round_robin_index;
            self.round_robin_index = (self.round_robin_index + 1) % self.servers.len();
            
            if self.servers[index].is_active {
                return Some(&mut self.servers[index]);
            }
            
            if self.round_robin_index == start {
                return None; // No active servers
            }
        }
    }
    
    // STRATEGY 2: Least Connections
    fn least_connections(&mut self) -> Option<&mut Server> {
        self.servers
            .iter_mut()
            .filter(|s| s.is_active)
            .min_by_key(|s| s.active_connections)
    }
    
    // STRATEGY 3: Consistent Hashing - Build ring
    fn build_hash_ring(&mut self, virtual_nodes: usize) {
        self.hash_ring.clear();
        
        for (idx, server) in self.servers.iter().enumerate() {
            if !server.is_active {
                continue;
            }
            
            for i in 0..virtual_nodes {
                let vnode = format!("{}#{}", server.address(), i);
                let hash = self.hash(&vnode);
                self.hash_ring.insert(hash, idx);
            }
        }
    }
    
    // Consistent Hashing - Get server
    fn consistent_hash(&mut self, key: &str) -> Option<&mut Server> {
        if self.hash_ring.is_empty() {
            return None;
        }
        
        let hash = self.hash(key);
        
        // Find the first server with hash >= key's hash
        let mut keys: Vec<&u64> = self.hash_ring.keys().collect();
        keys.sort();
        
        let server_idx = keys
            .iter()
            .find(|&&k| *k >= hash)
            .or_else(|| keys.first()) // Wrap around
            .and_then(|k| self.hash_ring.get(k))?;
        
        self.servers.get_mut(*server_idx)
    }
    
    fn get_server(&self, index: usize) -> Option<&Server> {
        self.servers.get(index)
    }
    
    fn get_server_mut(&mut self, index: usize) -> Option<&mut Server> {
        self.servers.get_mut(index)
    }
}

fn main() {
    let mut lb = LoadBalancer::new();
    
    lb.add_server("192.168.1.10".to_string(), 8080);
    lb.add_server("192.168.1.11".to_string(), 8080);
    lb.add_server("192.168.1.12".to_string(), 8080);
    
    println!("=== Round-Robin ===");
    for i in 0..6 {
        if let Some(srv) = lb.round_robin() {
            println!("Request {} -> {}", i, srv.address());
        }
    }
    
    println!("\n=== Least Connections ===");
    lb.get_server_mut(0).unwrap().active_connections = 5;
    lb.get_server_mut(1).unwrap().active_connections = 2;
    lb.get_server_mut(2).unwrap().active_connections = 8;
    
    for i in 0..4 {
        if let Some(srv) = lb.least_connections() {
            println!(
                "Request {} -> {} (connections: {})", 
                i, srv.address(), srv.active_connections
            );
            srv.active_connections += 1;
        }
    }
    
    println!("\n=== Consistent Hashing ===");
    lb.build_hash_ring(150);
    
    let clients = vec![
        "client-A", "client-B", "client-C", 
        "client-A", "client-D", "client-B"
    ];
    
    for client in clients {
        if let Some(srv) = lb.consistent_hash(client) {
            println!("{} -> {}", client, srv.address());
        }
    }
}
```

---

## Summary

**Load balancing strategies** are essential for distributing traffic across multiple servers efficiently:

- **Round-Robin**: Simple, sequential distribution. Best when servers have equal capacity and requests have similar processing times. Low overhead but doesn't account for server load or client affinity.

- **Least Connections**: Routes to the server with fewest active connections. Ideal for long-lived connections or varying request durations. Requires tracking connection state but provides better load distribution.

- **Consistent Hashing**: Maps clients and servers onto a hash ring, ensuring the same client typically reaches the same server. Excellent for session persistence and caching scenarios. Minimizes remapping when servers are added/removed (only ~1/n requests affected). Virtual nodes improve distribution uniformity.

Each strategy has trade-offs between simplicity, performance, and specific use-case requirements. Modern load balancers often combine multiple strategies or use weighted variations to optimize for their specific deployment scenarios.