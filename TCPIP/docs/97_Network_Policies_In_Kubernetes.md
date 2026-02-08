# Network Policies in Kubernetes

## Detailed Description

Network Policies in Kubernetes are specifications that define how groups of pods are allowed to communicate with each other and with other network endpoints. They act as a firewall at the pod level, providing fine-grained control over network traffic flow within a Kubernetes cluster.

### Core Concepts

**Pod Networking Fundamentals:**
In Kubernetes, every pod gets its own IP address, and pods can communicate with each other across nodes without NAT (Network Address Translation). This flat network model simplifies application development but requires proper security controls, which is where Network Policies come in.

**Network Policy Components:**
- **podSelector**: Defines which pods the policy applies to
- **policyTypes**: Specifies whether the policy governs Ingress, Egress, or both
- **ingress rules**: Control incoming traffic to selected pods
- **egress rules**: Control outgoing traffic from selected pods
- **namespaceSelector**: Selects namespaces for allowed traffic
- **ipBlock**: Specifies IP CIDR ranges for allowed traffic

**CNI Plugins:**
Container Network Interface (CNI) plugins are responsible for implementing the actual network policy enforcement. Not all CNI plugins support Network Policies. Popular CNI plugins that do include Calico, Cilium, Weave Net, and Romana. The CNI plugin handles the low-level networking details, including setting up network interfaces, IP address management, and applying firewall rules.

**Default Behavior:**
By default, pods accept traffic from any source. Once a Network Policy selects a pod, that pod becomes "isolated" and only accepts traffic explicitly allowed by the policy.

### How Network Policies Work

Network Policies use label selectors to identify pods and define rules using a whitelist approach. When multiple policies select the same pod, their rules are combined additively (union of all allowed traffic).

---

## Programming Examples

### C Example: Simulating Network Policy Enforcement

This example demonstrates a simplified network policy checker that could be part of a CNI plugin implementation:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>

#define MAX_RULES 100
#define MAX_LABELS 10

typedef struct {
    char key[64];
    char value[64];
} Label;

typedef struct {
    Label labels[MAX_LABELS];
    int label_count;
    char ip[16];
    int port;
} Pod;

typedef struct {
    char cidr[32];
    int port;
    char protocol[8];
} NetworkRule;

typedef struct {
    Label pod_selector[MAX_LABELS];
    int selector_count;
    NetworkRule ingress_rules[MAX_RULES];
    int ingress_count;
    NetworkRule egress_rules[MAX_RULES];
    int egress_count;
    bool allow_ingress;
    bool allow_egress;
} NetworkPolicy;

// Check if pod matches label selector
bool matches_selector(Pod *pod, Label *selectors, int selector_count) {
    if (selector_count == 0) return true; // Empty selector matches all
    
    for (int i = 0; i < selector_count; i++) {
        bool found = false;
        for (int j = 0; j < pod->label_count; j++) {
            if (strcmp(pod->labels[j].key, selectors[i].key) == 0 &&
                strcmp(pod->labels[j].value, selectors[i].value) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false; // All selectors must match
    }
    return true;
}

// Check if IP is in CIDR range
bool ip_in_cidr(const char *ip, const char *cidr) {
    char cidr_copy[32];
    strcpy(cidr_copy, cidr);
    
    char *slash = strchr(cidr_copy, '/');
    if (!slash) return false;
    
    *slash = '\0';
    int prefix_len = atoi(slash + 1);
    
    struct in_addr ip_addr, cidr_addr;
    inet_pton(AF_INET, ip, &ip_addr);
    inet_pton(AF_INET, cidr_copy, &cidr_addr);
    
    uint32_t mask = prefix_len ? htonl(~((1 << (32 - prefix_len)) - 1)) : 0;
    
    return (ip_addr.s_addr & mask) == (cidr_addr.s_addr & mask);
}

// Check if ingress traffic is allowed
bool check_ingress_allowed(Pod *pod, const char *source_ip, int dest_port,
                           NetworkPolicy *policies, int policy_count) {
    bool policy_applies = false;
    bool traffic_allowed = false;
    
    for (int i = 0; i < policy_count; i++) {
        if (!policies[i].allow_ingress) continue;
        
        if (matches_selector(pod, policies[i].pod_selector, 
                           policies[i].selector_count)) {
            policy_applies = true;
            
            // Check ingress rules
            for (int j = 0; j < policies[i].ingress_count; j++) {
                NetworkRule *rule = &policies[i].ingress_rules[j];
                
                if (ip_in_cidr(source_ip, rule->cidr) &&
                    (rule->port == 0 || rule->port == dest_port)) {
                    traffic_allowed = true;
                    break;
                }
            }
            
            if (traffic_allowed) break;
        }
    }
    
    // If no policy applies, traffic is allowed by default
    // If policy applies but no rule matches, traffic is denied
    return !policy_applies || traffic_allowed;
}

int main() {
    // Create a sample pod
    Pod web_pod = {
        .labels = {{"app", "web"}, {"tier", "frontend"}},
        .label_count = 2,
        .ip = "10.0.1.10",
        .port = 80
    };
    
    // Create a network policy
    NetworkPolicy policy = {
        .pod_selector = {{"app", "web"}},
        .selector_count = 1,
        .allow_ingress = true,
        .allow_egress = false
    };
    
    // Add ingress rule: allow from 10.0.2.0/24 on port 80
    strcpy(policy.ingress_rules[0].cidr, "10.0.2.0/24");
    policy.ingress_rules[0].port = 80;
    strcpy(policy.ingress_rules[0].protocol, "TCP");
    policy.ingress_count = 1;
    
    NetworkPolicy policies[] = {policy};
    int policy_count = 1;
    
    // Test traffic scenarios
    printf("Testing Network Policy Enforcement:\n");
    printf("=====================================\n\n");
    
    const char *test_ips[] = {"10.0.2.5", "10.0.3.5", "192.168.1.1"};
    int test_ports[] = {80, 443, 8080};
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            bool allowed = check_ingress_allowed(&web_pod, test_ips[i], 
                                                 test_ports[j], policies, 
                                                 policy_count);
            printf("Traffic from %s to port %d: %s\n", 
                   test_ips[i], test_ports[j], 
                   allowed ? "ALLOWED" : "DENIED");
        }
    }
    
    return 0;
}
```

### C++ Example: Network Policy Manager

A more sophisticated C++ implementation with object-oriented design:

```cpp
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <algorithm>
#include <netinet/in.h>
#include <arpa/inet.h>

class IPAddress {
private:
    uint32_t addr;
    
public:
    IPAddress(const std::string& ip) {
        struct in_addr in_addr;
        inet_pton(AF_INET, ip.c_str(), &in_addr);
        addr = ntohl(in_addr.s_addr);
    }
    
    bool inCIDR(const std::string& cidr) const {
        size_t slash_pos = cidr.find('/');
        if (slash_pos == std::string::npos) return false;
        
        std::string network = cidr.substr(0, slash_pos);
        int prefix_len = std::stoi(cidr.substr(slash_pos + 1));
        
        IPAddress network_addr(network);
        uint32_t mask = prefix_len ? (~((1U << (32 - prefix_len)) - 1)) : 0;
        
        return (addr & mask) == (network_addr.addr & mask);
    }
};

class NetworkRule {
public:
    std::vector<std::string> cidr_blocks;
    std::vector<int> ports;
    std::string protocol;
    std::map<std::string, std::string> pod_selector;
    std::map<std::string, std::string> namespace_selector;
    
    bool matches(const IPAddress& ip, int port, 
                const std::map<std::string, std::string>& labels) const {
        // Check CIDR blocks
        if (!cidr_blocks.empty()) {
            bool cidr_match = false;
            for (const auto& cidr : cidr_blocks) {
                if (ip.inCIDR(cidr)) {
                    cidr_match = true;
                    break;
                }
            }
            if (!cidr_match) return false;
        }
        
        // Check ports
        if (!ports.empty()) {
            if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
                return false;
            }
        }
        
        // Check pod selector
        if (!pod_selector.empty()) {
            for (const auto& selector : pod_selector) {
                auto it = labels.find(selector.first);
                if (it == labels.end() || it->second != selector.second) {
                    return false;
                }
            }
        }
        
        return true;
    }
};

class NetworkPolicy {
private:
    std::string name;
    std::string namespace_name;
    std::map<std::string, std::string> pod_selector;
    std::vector<NetworkRule> ingress_rules;
    std::vector<NetworkRule> egress_rules;
    bool has_ingress_policy;
    bool has_egress_policy;
    
public:
    NetworkPolicy(const std::string& name, const std::string& ns)
        : name(name), namespace_name(ns), 
          has_ingress_policy(false), has_egress_policy(false) {}
    
    void setPodSelector(const std::map<std::string, std::string>& selector) {
        pod_selector = selector;
    }
    
    void addIngressRule(const NetworkRule& rule) {
        ingress_rules.push_back(rule);
        has_ingress_policy = true;
    }
    
    void addEgressRule(const NetworkRule& rule) {
        egress_rules.push_back(rule);
        has_egress_policy = true;
    }
    
    bool appliesToPod(const std::map<std::string, std::string>& pod_labels) const {
        if (pod_selector.empty()) return true;
        
        for (const auto& selector : pod_selector) {
            auto it = pod_labels.find(selector.first);
            if (it == pod_labels.end() || it->second != selector.second) {
                return false;
            }
        }
        return true;
    }
    
    bool allowsIngress(const IPAddress& source_ip, int dest_port,
                      const std::map<std::string, std::string>& source_labels) const {
        if (!has_ingress_policy) return true;
        
        for (const auto& rule : ingress_rules) {
            if (rule.matches(source_ip, dest_port, source_labels)) {
                return true;
            }
        }
        return false;
    }
    
    bool allowsEgress(const IPAddress& dest_ip, int dest_port,
                     const std::map<std::string, std::string>& dest_labels) const {
        if (!has_egress_policy) return true;
        
        for (const auto& rule : egress_rules) {
            if (rule.matches(dest_ip, dest_port, dest_labels)) {
                return true;
            }
        }
        return false;
    }
    
    bool hasIngressPolicy() const { return has_ingress_policy; }
    bool hasEgressPolicy() const { return has_egress_policy; }
};

class NetworkPolicyController {
private:
    std::vector<std::shared_ptr<NetworkPolicy>> policies;
    
public:
    void addPolicy(std::shared_ptr<NetworkPolicy> policy) {
        policies.push_back(policy);
    }
    
    bool checkIngressAllowed(const std::map<std::string, std::string>& pod_labels,
                            const IPAddress& source_ip, int dest_port,
                            const std::map<std::string, std::string>& source_labels) {
        bool policy_applies = false;
        bool traffic_allowed = false;
        
        for (const auto& policy : policies) {
            if (policy->appliesToPod(pod_labels)) {
                if (policy->hasIngressPolicy()) {
                    policy_applies = true;
                    if (policy->allowsIngress(source_ip, dest_port, source_labels)) {
                        traffic_allowed = true;
                        break;
                    }
                }
            }
        }
        
        return !policy_applies || traffic_allowed;
    }
    
    bool checkEgressAllowed(const std::map<std::string, std::string>& pod_labels,
                           const IPAddress& dest_ip, int dest_port,
                           const std::map<std::string, std::string>& dest_labels) {
        bool policy_applies = false;
        bool traffic_allowed = false;
        
        for (const auto& policy : policies) {
            if (policy->appliesToPod(pod_labels)) {
                if (policy->hasEgressPolicy()) {
                    policy_applies = true;
                    if (policy->allowsEgress(dest_ip, dest_port, dest_labels)) {
                        traffic_allowed = true;
                        break;
                    }
                }
            }
        }
        
        return !policy_applies || traffic_allowed;
    }
};

int main() {
    NetworkPolicyController controller;
    
    // Create a network policy for database pods
    auto db_policy = std::make_shared<NetworkPolicy>("db-policy", "default");
    db_policy->setPodSelector({{"app", "database"}});
    
    // Allow ingress from app tier on port 5432
    NetworkRule db_ingress;
    db_ingress.pod_selector = {{"tier", "app"}};
    db_ingress.ports = {5432};
    db_ingress.protocol = "TCP";
    db_policy->addIngressRule(db_ingress);
    
    // Deny all egress except DNS
    NetworkRule dns_egress;
    dns_egress.ports = {53};
    dns_egress.protocol = "UDP";
    db_policy->addEgressRule(dns_egress);
    
    controller.addPolicy(db_policy);
    
    // Test scenarios
    std::map<std::string, std::string> db_labels = {{"app", "database"}};
    std::map<std::string, std::string> app_labels = {{"tier", "app"}};
    std::map<std::string, std::string> web_labels = {{"tier", "web"}};
    
    std::cout << "Network Policy Enforcement Tests:\n";
    std::cout << "===================================\n\n";
    
    IPAddress app_ip("10.0.2.5");
    IPAddress web_ip("10.0.1.5");
    
    bool allowed = controller.checkIngressAllowed(db_labels, app_ip, 5432, app_labels);
    std::cout << "App tier to DB port 5432: " 
              << (allowed ? "ALLOWED" : "DENIED") << "\n";
    
    allowed = controller.checkIngressAllowed(db_labels, web_ip, 5432, web_labels);
    std::cout << "Web tier to DB port 5432: " 
              << (allowed ? "ALLOWED" : "DENIED") << "\n";
    
    IPAddress dns_ip("10.96.0.10");
    allowed = controller.checkEgressAllowed(db_labels, dns_ip, 53, {});
    std::cout << "DB to DNS port 53: " 
              << (allowed ? "ALLOWED" : "DENIED") << "\n";
    
    IPAddress external_ip("8.8.8.8");
    allowed = controller.checkEgressAllowed(db_labels, external_ip, 443, {});
    std::cout << "DB to external HTTPS: " 
              << (allowed ? "ALLOWED" : "DENIED") << "\n";
    
    return 0;
}
```

### Rust Example: Safe Network Policy Engine

A Rust implementation emphasizing safety and concurrency:

```rust
use std::collections::HashMap;
use std::net::Ipv4Addr;
use std::str::FromStr;

#[derive(Debug, Clone)]
struct IpNetwork {
    addr: u32,
    prefix_len: u8,
}

impl IpNetwork {
    fn new(cidr: &str) -> Result<Self, String> {
        let parts: Vec<&str> = cidr.split('/').collect();
        if parts.len() != 2 {
            return Err("Invalid CIDR format".to_string());
        }
        
        let addr = Ipv4Addr::from_str(parts[0])
            .map_err(|_| "Invalid IP address")?;
        let prefix_len = parts[1].parse::<u8>()
            .map_err(|_| "Invalid prefix length")?;
        
        Ok(IpNetwork {
            addr: u32::from(addr),
            prefix_len,
        })
    }
    
    fn contains(&self, ip: &Ipv4Addr) -> bool {
        let ip_u32 = u32::from(*ip);
        let mask = if self.prefix_len == 0 {
            0
        } else {
            !((1u32 << (32 - self.prefix_len)) - 1)
        };
        
        (ip_u32 & mask) == (self.addr & mask)
    }
}

#[derive(Debug, Clone)]
struct PortRange {
    start: u16,
    end: u16,
}

impl PortRange {
    fn single(port: u16) -> Self {
        PortRange { start: port, end: port }
    }
    
    fn contains(&self, port: u16) -> bool {
        port >= self.start && port <= self.end
    }
}

#[derive(Debug, Clone)]
struct NetworkRule {
    cidr_blocks: Vec<IpNetwork>,
    ports: Vec<PortRange>,
    protocol: Option<String>,
    pod_selector: HashMap<String, String>,
    namespace_selector: HashMap<String, String>,
}

impl NetworkRule {
    fn new() -> Self {
        NetworkRule {
            cidr_blocks: Vec::new(),
            ports: Vec::new(),
            protocol: None,
            pod_selector: HashMap::new(),
            namespace_selector: HashMap::new(),
        }
    }
    
    fn matches(&self, ip: &Ipv4Addr, port: u16, labels: &HashMap<String, String>) -> bool {
        // Check CIDR blocks
        if !self.cidr_blocks.is_empty() {
            if !self.cidr_blocks.iter().any(|cidr| cidr.contains(ip)) {
                return false;
            }
        }
        
        // Check ports
        if !self.ports.is_empty() {
            if !self.ports.iter().any(|range| range.contains(port)) {
                return false;
            }
        }
        
        // Check pod selector
        if !self.pod_selector.is_empty() {
            for (key, value) in &self.pod_selector {
                match labels.get(key) {
                    Some(label_value) if label_value == value => continue,
                    _ => return false,
                }
            }
        }
        
        true
    }
}

#[derive(Debug, Clone)]
struct NetworkPolicy {
    name: String,
    namespace: String,
    pod_selector: HashMap<String, String>,
    ingress_rules: Vec<NetworkRule>,
    egress_rules: Vec<NetworkRule>,
    policy_types: Vec<String>,
}

impl NetworkPolicy {
    fn new(name: String, namespace: String) -> Self {
        NetworkPolicy {
            name,
            namespace,
            pod_selector: HashMap::new(),
            ingress_rules: Vec::new(),
            egress_rules: Vec::new(),
            policy_types: Vec::new(),
        }
    }
    
    fn applies_to_pod(&self, pod_labels: &HashMap<String, String>) -> bool {
        if self.pod_selector.is_empty() {
            return true;
        }
        
        for (key, value) in &self.pod_selector {
            match pod_labels.get(key) {
                Some(label_value) if label_value == value => continue,
                _ => return false,
            }
        }
        
        true
    }
    
    fn has_ingress_policy(&self) -> bool {
        self.policy_types.contains(&"Ingress".to_string())
    }
    
    fn has_egress_policy(&self) -> bool {
        self.policy_types.contains(&"Egress".to_string())
    }
    
    fn allows_ingress(&self, source_ip: &Ipv4Addr, dest_port: u16, 
                     source_labels: &HashMap<String, String>) -> bool {
        if !self.has_ingress_policy() {
            return true;
        }
        
        self.ingress_rules.iter()
            .any(|rule| rule.matches(source_ip, dest_port, source_labels))
    }
    
    fn allows_egress(&self, dest_ip: &Ipv4Addr, dest_port: u16,
                    dest_labels: &HashMap<String, String>) -> bool {
        if !self.has_egress_policy() {
            return true;
        }
        
        self.egress_rules.iter()
            .any(|rule| rule.matches(dest_ip, dest_port, dest_labels))
    }
}

struct NetworkPolicyController {
    policies: Vec<NetworkPolicy>,
}

impl NetworkPolicyController {
    fn new() -> Self {
        NetworkPolicyController {
            policies: Vec::new(),
        }
    }
    
    fn add_policy(&mut self, policy: NetworkPolicy) {
        self.policies.push(policy);
    }
    
    fn check_ingress_allowed(&self, pod_labels: &HashMap<String, String>,
                            source_ip: &Ipv4Addr, dest_port: u16,
                            source_labels: &HashMap<String, String>) -> bool {
        let applicable_policies: Vec<&NetworkPolicy> = self.policies.iter()
            .filter(|p| p.applies_to_pod(pod_labels))
            .collect();
        
        if applicable_policies.is_empty() {
            return true; // No policy = allow all
        }
        
        let ingress_policies: Vec<&NetworkPolicy> = applicable_policies.iter()
            .filter(|p| p.has_ingress_policy())
            .copied()
            .collect();
        
        if ingress_policies.is_empty() {
            return true; // No ingress policy = allow all ingress
        }
        
        // At least one policy must allow the traffic
        ingress_policies.iter()
            .any(|p| p.allows_ingress(source_ip, dest_port, source_labels))
    }
    
    fn check_egress_allowed(&self, pod_labels: &HashMap<String, String>,
                           dest_ip: &Ipv4Addr, dest_port: u16,
                           dest_labels: &HashMap<String, String>) -> bool {
        let applicable_policies: Vec<&NetworkPolicy> = self.policies.iter()
            .filter(|p| p.applies_to_pod(pod_labels))
            .collect();
        
        if applicable_policies.is_empty() {
            return true;
        }
        
        let egress_policies: Vec<&NetworkPolicy> = applicable_policies.iter()
            .filter(|p| p.has_egress_policy())
            .copied()
            .collect();
        
        if egress_policies.is_empty() {
            return true;
        }
        
        egress_policies.iter()
            .any(|p| p.allows_egress(dest_ip, dest_port, dest_labels))
    }
}

fn main() {
    let mut controller = NetworkPolicyController::new();
    
    // Create a network policy for frontend pods
    let mut frontend_policy = NetworkPolicy::new(
        "frontend-policy".to_string(),
        "default".to_string()
    );
    
    frontend_policy.pod_selector.insert("app".to_string(), "frontend".to_string());
    frontend_policy.policy_types.push("Ingress".to_string());
    frontend_policy.policy_types.push("Egress".to_string());
    
    // Allow ingress from load balancer subnet
    let mut lb_ingress_rule = NetworkRule::new();
    lb_ingress_rule.cidr_blocks.push(
        IpNetwork::new("10.0.100.0/24").unwrap()
    );
    lb_ingress_rule.ports.push(PortRange::single(80));
    lb_ingress_rule.ports.push(PortRange::single(443));
    frontend_policy.ingress_rules.push(lb_ingress_rule);
    
    // Allow egress to backend on port 8080
    let mut backend_egress_rule = NetworkRule::new();
    backend_egress_rule.pod_selector.insert("tier".to_string(), "backend".to_string());
    backend_egress_rule.ports.push(PortRange::single(8080));
    frontend_policy.egress_rules.push(backend_egress_rule);
    
    // Allow egress to DNS
    let mut dns_egress_rule = NetworkRule::new();
    dns_egress_rule.cidr_blocks.push(
        IpNetwork::new("10.96.0.10/32").unwrap()
    );
    dns_egress_rule.ports.push(PortRange::single(53));
    frontend_policy.egress_rules.push(dns_egress_rule);
    
    controller.add_policy(frontend_policy);
    
    // Test scenarios
    println!("Network Policy Enforcement Tests:");
    println!("===================================\n");
    
    let frontend_labels = HashMap::from([
        ("app".to_string(), "frontend".to_string()),
    ]);
    
    let backend_labels = HashMap::from([
        ("tier".to_string(), "backend".to_string()),
    ]);
    
    let lb_ip = Ipv4Addr::new(10, 0, 100, 5);
    let external_ip = Ipv4Addr::new(203, 0, 113, 1);
    let backend_ip = Ipv4Addr::new(10, 0, 2, 10);
    let dns_ip = Ipv4Addr::new(10, 96, 0, 10);
    
    // Test ingress
    let allowed = controller.check_ingress_allowed(
        &frontend_labels, &lb_ip, 80, &HashMap::new()
    );
    println!("Load balancer to frontend:80 - {}", 
             if allowed { "ALLOWED" } else { "DENIED" });
    
    let allowed = controller.check_ingress_allowed(
        &frontend_labels, &external_ip, 80, &HashMap::new()
    );
    println!("External to frontend:80 - {}", 
             if allowed { "ALLOWED" } else { "DENIED" });
    
    // Test egress
    let allowed = controller.check_egress_allowed(
        &frontend_labels, &backend_ip, 8080, &backend_labels
    );
    println!("Frontend to backend:8080 - {}", 
             if allowed { "ALLOWED" } else { "DENIED" });
    
    let allowed = controller.check_egress_allowed(
        &frontend_labels, &dns_ip, 53, &HashMap::new()
    );
    println!("Frontend to DNS:53 - {}", 
             if allowed { "ALLOWED" } else { "DENIED" });
    
    let allowed = controller.check_egress_allowed(
        &frontend_labels, &external_ip, 443, &HashMap::new()
    );
    println!("Frontend to external:443 - {}", 
             if allowed { "ALLOWED" } else { "DENIED" });
}
```

---

## Summary

Network Policies in Kubernetes provide critical security controls for pod-to-pod communication within clusters. They operate using a whitelist model where pods become isolated once a policy selects them, accepting only explicitly allowed traffic. The effectiveness of Network Policies depends on the underlying CNI plugin implementation, which handles the actual enforcement through iptables rules or eBPF programs.

Key takeaways:
- Network Policies use label selectors to target pods and define traffic rules
- Policies are additive—multiple policies combine to allow more traffic
- Both Ingress and Egress traffic can be controlled independently
- CNI plugins like Calico, Cilium, and Weave Net are required for enforcement
- Default behavior is to allow all traffic until a policy restricts it

The programming examples demonstrate how policy matching logic works internally, including label selector matching, CIDR block verification, and policy rule evaluation. Understanding these mechanisms helps in designing effective network security policies and troubleshooting connectivity issues in production Kubernetes environments.