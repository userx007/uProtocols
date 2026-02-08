# Network Switching Detection

## Detailed Description

Network switching detection is the process of monitoring and responding to changes in the active network interface on a device. Modern devices frequently transition between different network types (WiFi, cellular data, Ethernet, VPN) due to mobility, signal strength variations, or manual user actions. Detecting these transitions is crucial for maintaining application connectivity, optimizing network performance, and providing seamless user experiences.

### Why Network Switching Matters

When a device switches networks, several critical changes occur:

- **IP address changes** - The device typically receives a new IP address from the new network
- **Network characteristics change** - Bandwidth, latency, and reliability differ significantly between WiFi, cellular, and Ethernet
- **Existing connections break** - TCP connections bound to the old interface may become invalid
- **Routing tables update** - The OS updates routes to prefer the new interface
- **DNS servers may change** - Different networks often use different DNS resolvers

Applications that don't handle network switches properly may experience:
- Connection timeouts and failures
- Data loss or corruption
- Poor user experience with hanging operations
- Wasted resources attempting to use dead connections

### Detection Approaches

**1. Interface Monitoring (Platform-Specific)**
- Linux: Monitor netlink socket for RTM_NEWADDR, RTM_DELADDR messages
- Windows: Use NotifyIpInterfaceChange or NetworkChange events
- macOS/iOS: Use SystemConfiguration framework's SCNetworkReachability
- Android: Monitor ConnectivityManager callbacks

**2. Active Probing**
- Periodically test connectivity on different interfaces
- Detect changes by attempting connections
- Less efficient but more portable

**3. Connection-Level Detection**
- Monitor socket errors that indicate interface loss
- Detect ENETUNREACH, EHOSTUNREACH errors
- React to connection failures

### Key Challenges

- **Race conditions** - Connections may be in-flight during switches
- **Graceful migration** - Moving active sessions to new interfaces
- **Multiple interfaces** - Devices often have several interfaces active simultaneously
- **Battery impact** - Aggressive monitoring drains battery on mobile devices
- **Platform differences** - Each OS provides different APIs and behaviors

## Programming Examples

### C Implementation (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096

// Structure to track interface information
typedef struct {
    char name[IF_NAMESIZE];
    int index;
    int family;
    char address[INET6_ADDRSTRLEN];
} InterfaceInfo;

// Callback function type for network changes
typedef void (*NetworkChangeCallback)(InterfaceInfo *info, int is_added);

// Parse netlink message to extract interface information
void parse_rtnetlink_message(struct nlmsghdr *nlh, NetworkChangeCallback callback) {
    struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    struct rtattr *rta = IFA_RTA(ifa);
    int rta_len = IFA_PAYLOAD(nlh);
    
    InterfaceInfo info;
    memset(&info, 0, sizeof(info));
    
    info.index = ifa->ifa_index;
    info.family = ifa->ifa_family;
    
    // Get interface name
    if_indextoname(ifa->ifa_index, info.name);
    
    // Parse attributes
    while (RTA_OK(rta, rta_len)) {
        if (rta->rta_type == IFA_ADDRESS || rta->rta_type == IFA_LOCAL) {
            void *addr_data = RTA_DATA(rta);
            
            if (ifa->ifa_family == AF_INET) {
                inet_ntop(AF_INET, addr_data, info.address, sizeof(info.address));
            } else if (ifa->ifa_family == AF_INET6) {
                inet_ntop(AF_INET6, addr_data, info.address, sizeof(info.address));
            }
        }
        rta = RTA_NEXT(rta, rta_len);
    }
    
    // Determine if address was added or removed
    int is_added = (nlh->nlmsg_type == RTM_NEWADDR);
    
    if (callback) {
        callback(&info, is_added);
    }
}

// Monitor network interface changes using netlink
int monitor_network_changes(NetworkChangeCallback callback) {
    int sock_fd;
    struct sockaddr_nl sa;
    char buffer[BUFFER_SIZE];
    
    // Create netlink socket
    sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // Bind to receive network interface notifications
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_LINK;
    
    if (bind(sock_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        close(sock_fd);
        return -1;
    }
    
    printf("Monitoring network interface changes...\n");
    
    // Main event loop
    while (1) {
        ssize_t len = recv(sock_fd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            perror("recv");
            break;
        }
        
        // Process all messages in the buffer
        struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;
        while (NLMSG_OK(nlh, len)) {
            switch (nlh->nlmsg_type) {
                case RTM_NEWADDR:
                case RTM_DELADDR:
                    parse_rtnetlink_message(nlh, callback);
                    break;
                    
                case RTM_NEWLINK:
                case RTM_DELLINK:
                    // Handle link state changes
                    printf("Link state changed\n");
                    break;
            }
            
            nlh = NLMSG_NEXT(nlh, len);
        }
    }
    
    close(sock_fd);
    return 0;
}

// Example callback function
void on_network_change(InterfaceInfo *info, int is_added) {
    printf("Interface %s (%d): %s %s address %s\n",
           info->name,
           info->index,
           is_added ? "ADDED" : "REMOVED",
           info->family == AF_INET ? "IPv4" : "IPv6",
           info->address);
    
    // Application-specific handling
    if (is_added && strcmp(info->name, "wlan0") == 0) {
        printf("  -> WiFi connected, may want to reconnect active sessions\n");
    } else if (!is_added && strcmp(info->name, "eth0") == 0) {
        printf("  -> Ethernet disconnected, falling back to other interfaces\n");
    }
}

int main() {
    monitor_network_changes(on_network_change);
    return 0;
}
```

### C++ Implementation (Cross-Platform)

```cpp
#include <iostream>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#endif

class NetworkMonitor {
public:
    enum class InterfaceType {
        UNKNOWN,
        ETHERNET,
        WIFI,
        CELLULAR,
        LOOPBACK
    };
    
    struct InterfaceInfo {
        std::string name;
        InterfaceType type;
        std::string ipv4_address;
        std::string ipv6_address;
        bool is_up;
        
        bool operator==(const InterfaceInfo& other) const {
            return name == other.name &&
                   ipv4_address == other.ipv4_address &&
                   ipv6_address == other.ipv6_address &&
                   is_up == other.is_up;
        }
        
        bool operator!=(const InterfaceInfo& other) const {
            return !(*this == other);
        }
    };
    
    using ChangeCallback = std::function<void(const InterfaceInfo&, bool added)>;
    
    NetworkMonitor() : running_(false) {}
    
    ~NetworkMonitor() {
        stop();
    }
    
    void start(ChangeCallback callback, int poll_interval_ms = 1000) {
        if (running_.exchange(true)) {
            return; // Already running
        }
        
        callback_ = callback;
        
        // Initial scan
        current_interfaces_ = scan_interfaces();
        
        // Start monitoring thread
        monitor_thread_ = std::thread([this, poll_interval_ms]() {
            while (running_) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(poll_interval_ms));
                check_for_changes();
            }
        });
    }
    
    void stop() {
        if (running_.exchange(false)) {
            if (monitor_thread_.joinable()) {
                monitor_thread_.join();
            }
        }
    }
    
    std::vector<InterfaceInfo> get_active_interfaces() const {
        return current_interfaces_;
    }
    
private:
    std::atomic<bool> running_;
    std::thread monitor_thread_;
    ChangeCallback callback_;
    std::vector<InterfaceInfo> current_interfaces_;
    
    InterfaceType guess_interface_type(const std::string& name) {
        if (name.find("eth") == 0 || name.find("en") == 0) {
            return InterfaceType::ETHERNET;
        } else if (name.find("wlan") == 0 || name.find("wifi") == 0 ||
                   name.find("wl") == 0) {
            return InterfaceType::WIFI;
        } else if (name.find("cellular") == 0 || name.find("rmnet") == 0 ||
                   name.find("pdp_ip") == 0) {
            return InterfaceType::CELLULAR;
        } else if (name == "lo" || name.find("loopback") == 0) {
            return InterfaceType::LOOPBACK;
        }
        return InterfaceType::UNKNOWN;
    }
    
    std::vector<InterfaceInfo> scan_interfaces() {
        std::vector<InterfaceInfo> interfaces;
        
#ifdef _WIN32
        // Windows implementation using GetAdaptersAddresses
        ULONG buffer_size = 15000;
        PIP_ADAPTER_ADDRESSES addresses = nullptr;
        
        do {
            addresses = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
            ULONG result = GetAdaptersAddresses(
                AF_UNSPEC,
                GAA_FLAG_INCLUDE_PREFIX,
                nullptr,
                addresses,
                &buffer_size
            );
            
            if (result == ERROR_BUFFER_OVERFLOW) {
                free(addresses);
                addresses = nullptr;
            } else if (result == NO_ERROR) {
                break;
            } else {
                free(addresses);
                return interfaces;
            }
        } while (addresses == nullptr);
        
        for (PIP_ADAPTER_ADDRESSES addr = addresses; addr; addr = addr->Next) {
            if (addr->OperStatus != IfOperStatusUp) {
                continue;
            }
            
            InterfaceInfo info;
            info.name = addr->AdapterName;
            info.type = guess_interface_type(info.name);
            info.is_up = true;
            
            for (PIP_ADAPTER_UNICAST_ADDRESS ua = addr->FirstUnicastAddress;
                 ua; ua = ua->Next) {
                sockaddr* sa = ua->Address.lpSockaddr;
                char addr_str[INET6_ADDRSTRLEN];
                
                if (sa->sa_family == AF_INET) {
                    inet_ntop(AF_INET,
                             &((sockaddr_in*)sa)->sin_addr,
                             addr_str, sizeof(addr_str));
                    info.ipv4_address = addr_str;
                } else if (sa->sa_family == AF_INET6) {
                    inet_ntop(AF_INET6,
                             &((sockaddr_in6*)sa)->sin6_addr,
                             addr_str, sizeof(addr_str));
                    info.ipv6_address = addr_str;
                }
            }
            
            interfaces.push_back(info);
        }
        
        free(addresses);
#else
        // Unix/Linux implementation using getifaddrs
        struct ifaddrs* ifaddr;
        if (getifaddrs(&ifaddr) == -1) {
            return interfaces;
        }
        
        std::map<std::string, InterfaceInfo> iface_map;
        
        for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) {
                continue;
            }
            
            std::string name = ifa->ifa_name;
            auto& info = iface_map[name];
            
            if (info.name.empty()) {
                info.name = name;
                info.type = guess_interface_type(name);
                info.is_up = (ifa->ifa_flags & IFF_UP) != 0;
            }
            
            int family = ifa->ifa_addr->sa_family;
            char addr_str[INET6_ADDRSTRLEN];
            
            if (family == AF_INET) {
                void* addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
                inet_ntop(AF_INET, addr, addr_str, sizeof(addr_str));
                info.ipv4_address = addr_str;
            } else if (family == AF_INET6) {
                void* addr = &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
                inet_ntop(AF_INET6, addr, addr_str, sizeof(addr_str));
                // Skip link-local addresses
                if (strncmp(addr_str, "fe80:", 5) != 0) {
                    info.ipv6_address = addr_str;
                }
            }
        }
        
        freeifaddrs(ifaddr);
        
        for (const auto& pair : iface_map) {
            if (pair.second.is_up &&
                (!pair.second.ipv4_address.empty() ||
                 !pair.second.ipv6_address.empty())) {
                interfaces.push_back(pair.second);
            }
        }
#endif
        
        return interfaces;
    }
    
    void check_for_changes() {
        auto new_interfaces = scan_interfaces();
        
        // Find removed interfaces
        for (const auto& old_iface : current_interfaces_) {
            bool found = false;
            for (const auto& new_iface : new_interfaces) {
                if (old_iface.name == new_iface.name) {
                    found = true;
                    // Check if properties changed
                    if (old_iface != new_iface) {
                        callback_(new_iface, false); // Removed old
                        callback_(new_iface, true);  // Added new
                    }
                    break;
                }
            }
            if (!found && callback_) {
                callback_(old_iface, false); // Removed
            }
        }
        
        // Find added interfaces
        for (const auto& new_iface : new_interfaces) {
            bool found = false;
            for (const auto& old_iface : current_interfaces_) {
                if (old_iface.name == new_iface.name) {
                    found = true;
                    break;
                }
            }
            if (!found && callback_) {
                callback_(new_iface, true); // Added
            }
        }
        
        current_interfaces_ = new_interfaces;
    }
};

int main() {
    NetworkMonitor monitor;
    
    monitor.start([](const NetworkMonitor::InterfaceInfo& info, bool added) {
        std::cout << (added ? "ADDED: " : "REMOVED: ")
                  << info.name;
        
        if (!info.ipv4_address.empty()) {
            std::cout << " IPv4: " << info.ipv4_address;
        }
        if (!info.ipv6_address.empty()) {
            std::cout << " IPv6: " << info.ipv6_address;
        }
        
        std::cout << std::endl;
        
        // Application logic for handling network changes
        if (added) {
            std::cout << "  -> Reconnecting active sessions to new interface\n";
        } else {
            std::cout << "  -> Cleaning up connections on removed interface\n";
        }
    });
    
    std::cout << "Monitoring network changes. Press Enter to exit...\n";
    std::cin.get();
    
    monitor.stop();
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

#[derive(Debug, Clone, PartialEq)]
enum InterfaceType {
    Unknown,
    Ethernet,
    WiFi,
    Cellular,
    Loopback,
}

#[derive(Debug, Clone, PartialEq)]
struct InterfaceInfo {
    name: String,
    iface_type: InterfaceType,
    ipv4_addresses: Vec<Ipv4Addr>,
    ipv6_addresses: Vec<Ipv6Addr>,
    is_up: bool,
}

impl InterfaceInfo {
    fn new(name: String) -> Self {
        let iface_type = Self::guess_type(&name);
        
        Self {
            name,
            iface_type,
            ipv4_addresses: Vec::new(),
            ipv6_addresses: Vec::new(),
            is_up: false,
        }
    }
    
    fn guess_type(name: &str) -> InterfaceType {
        if name.starts_with("eth") || name.starts_with("en") {
            InterfaceType::Ethernet
        } else if name.starts_with("wlan") || name.starts_with("wifi") 
                  || name.starts_with("wl") {
            InterfaceType::WiFi
        } else if name.starts_with("cellular") || name.starts_with("rmnet") 
                  || name.starts_with("pdp_ip") {
            InterfaceType::Cellular
        } else if name == "lo" || name.starts_with("loopback") {
            InterfaceType::Loopback
        } else {
            InterfaceType::Unknown
        }
    }
}

struct NetworkMonitor {
    interfaces: Arc<Mutex<HashMap<String, InterfaceInfo>>>,
    running: Arc<Mutex<bool>>,
}

impl NetworkMonitor {
    fn new() -> Self {
        Self {
            interfaces: Arc::new(Mutex::new(HashMap::new())),
            running: Arc::new(Mutex::new(false)),
        }
    }
    
    fn scan_interfaces() -> HashMap<String, InterfaceInfo> {
        use if_addrs::get_if_addrs;
        
        let mut interfaces = HashMap::new();
        
        if let Ok(if_addrs) = get_if_addrs() {
            for iface in if_addrs {
                let entry = interfaces
                    .entry(iface.name.clone())
                    .or_insert_with(|| InterfaceInfo::new(iface.name.clone()));
                
                // Mark as up (if_addrs only returns up interfaces)
                entry.is_up = true;
                
                match iface.addr {
                    if_addrs::IfAddr::V4(ref addr) => {
                        entry.ipv4_addresses.push(addr.ip);
                    }
                    if_addrs::IfAddr::V6(ref addr) => {
                        // Skip link-local addresses
                        if !addr.ip.is_loopback() 
                           && !format!("{}", addr.ip).starts_with("fe80:") {
                            entry.ipv6_addresses.push(addr.ip);
                        }
                    }
                }
            }
        }
        
        interfaces
    }
    
    fn start<F>(&self, mut callback: F, poll_interval: Duration)
    where
        F: FnMut(&InterfaceInfo, bool) + Send + 'static,
    {
        let mut running = self.running.lock().unwrap();
        if *running {
            return; // Already running
        }
        *running = true;
        drop(running);
        
        // Initial scan
        let initial = Self::scan_interfaces();
        *self.interfaces.lock().unwrap() = initial.clone();
        
        let interfaces = Arc::clone(&self.interfaces);
        let running = Arc::clone(&self.running);
        
        thread::spawn(move || {
            while *running.lock().unwrap() {
                thread::sleep(poll_interval);
                
                let new_interfaces = Self::scan_interfaces();
                let mut current = interfaces.lock().unwrap();
                
                // Detect removed interfaces
                let removed: Vec<_> = current
                    .iter()
                    .filter(|(name, _)| !new_interfaces.contains_key(*name))
                    .map(|(_, info)| info.clone())
                    .collect();
                
                for info in removed {
                    callback(&info, false);
                }
                
                // Detect added or changed interfaces
                for (name, new_info) in &new_interfaces {
                    match current.get(name) {
                        Some(old_info) if old_info != new_info => {
                            // Changed
                            callback(old_info, false);
                            callback(new_info, true);
                        }
                        None => {
                            // Added
                            callback(new_info, true);
                        }
                        _ => {} // No change
                    }
                }
                
                *current = new_interfaces;
            }
        });
    }
    
    fn stop(&self) {
        *self.running.lock().unwrap() = false;
    }
    
    fn get_active_interfaces(&self) -> Vec<InterfaceInfo> {
        self.interfaces
            .lock()
            .unwrap()
            .values()
            .cloned()
            .collect()
    }
}

// Application-level connection manager that handles network switches
struct ConnectionManager {
    active_connections: Arc<Mutex<Vec<Connection>>>,
}

#[derive(Clone)]
struct Connection {
    id: usize,
    interface: String,
    remote_addr: String,
}

impl ConnectionManager {
    fn new() -> Self {
        Self {
            active_connections: Arc::new(Mutex::new(Vec::new())),
        }
    }
    
    fn handle_network_change(&self, info: &InterfaceInfo, added: bool) {
        if added {
            println!("Network added: {} ({:?})", info.name, info.iface_type);
            
            if !info.ipv4_addresses.is_empty() {
                println!("  IPv4: {:?}", info.ipv4_addresses);
            }
            if !info.ipv6_addresses.is_empty() {
                println!("  IPv6: {:?}", info.ipv6_addresses);
            }
            
            // Reconnect or migrate connections to new interface
            self.reconnect_on_interface(&info.name);
            
        } else {
            println!("Network removed: {}", info.name);
            
            // Close connections on removed interface
            self.close_connections_on_interface(&info.name);
        }
    }
    
    fn reconnect_on_interface(&self, interface: &str) {
        println!("  -> Attempting to reconnect sessions on {}", interface);
        
        // In a real application, you would:
        // 1. Create new sockets bound to the new interface
        // 2. Re-establish TCP connections
        // 3. Resume application-level protocols (e.g., TLS handshake)
        // 4. Notify application layer of new connection
    }
    
    fn close_connections_on_interface(&self, interface: &str) {
        let mut connections = self.active_connections.lock().unwrap();
        connections.retain(|conn| {
            if conn.interface == interface {
                println!("  -> Closing connection {} on {}", conn.id, interface);
                false
            } else {
                true
            }
        });
    }
}

fn main() {
    // Add if_addrs to Cargo.toml:
    // [dependencies]
    // if-addrs = "0.10"
    
    let monitor = NetworkMonitor::new();
    let conn_manager = ConnectionManager::new();
    
    println!("Starting network monitor...");
    
    monitor.start(
        move |info, added| {
            conn_manager.handle_network_change(info, added);
        },
        Duration::from_secs(2),
    );
    
    println!("Monitoring network changes. Press Enter to exit...");
    let mut input = String::new();
    std::io::stdin().read_line(&mut input).unwrap();
    
    monitor.stop();
    thread::sleep(Duration::from_millis(100)); // Allow thread to finish
}
```

## Summary

Network switching detection is essential for building robust networked applications that gracefully handle transitions between different network interfaces. The key aspects include:

**Core Concepts:**
- Monitoring OS-level network interface changes (address additions/removals, link state)
- Detecting switches between WiFi, cellular, and Ethernet connections
- Handling the implications of IP address changes and connection disruptions

**Implementation Approaches:**
- **Platform-specific APIs** (Linux netlink, Windows NotifyIpInterfaceChange, iOS/macOS SystemConfiguration) provide real-time notifications
- **Polling-based detection** offers cross-platform compatibility at the cost of latency and efficiency
- **Hybrid approaches** balance responsiveness with portability

**Best Practices:**
- Maintain a registry of active network interfaces and their properties
- Implement callbacks or event handlers for network change notifications
- Gracefully close connections on removed interfaces
- Attempt reconnection on newly available interfaces
- Consider network characteristics (bandwidth, latency, cost) when choosing interfaces
- Handle race conditions during network transitions
- Optimize polling intervals to balance responsiveness with resource usage

Modern applications must handle network switching seamlessly to provide good user experiences on mobile and laptop devices that frequently move between networks. Proper detection and handling of these transitions prevents connection failures, reduces latency during switches, and enables applications to optimize their behavior based on the current network type.