# ARP Protocol Implementation Examples

This repository contains comprehensive implementations of the Address Resolution Protocol (ARP) in both C/C++ and Rust, along with detailed documentation.

## Contents

- **arp_implementation.c** - Full-featured C implementation
- **arp_implementation.rs** - Modern Rust implementation
- **ARP_Protocol_Documentation.md** - Complete protocol documentation
- **Cargo.toml** - Rust project configuration

## Features

Both implementations include:

✅ **Core ARP Operations**
- ARP Request creation and sending
- ARP Reply handling
- Gratuitous ARP generation
- Packet serialization/deserialization

✅ **ARP Cache Management**
- Dynamic and static entries
- Timeout-based expiration
- Cache lookup and updates
- Cache cleanup operations

✅ **Security Features**
- ARP spoof detection
- Rate limiting capabilities
- Validation mechanisms
- Security logging

✅ **Production-Ready Features**
- Comprehensive error handling
- Memory-safe operations (Rust)
- Detailed logging and debugging
- Well-documented code

## Requirements

### C/C++ Implementation

**System Requirements:**
- Linux operating system (uses Linux-specific headers)
- GCC or Clang compiler
- Root privileges (for raw socket access)

**Dependencies:**
```bash
# Ubuntu/Debian
sudo apt-get install build-essential

# RHEL/CentOS/Fedora
sudo yum install gcc make

# Arch Linux
sudo pacman -S base-devel
```

### Rust Implementation

**System Requirements:**
- Rust 1.70 or later
- Cargo (Rust's package manager)

**Install Rust:**
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

## Compilation

### C/C++ Version

```bash
# Compile with standard flags
gcc -o arp_implementation arp_implementation.c -Wall -Wextra

# Compile with optimizations
gcc -o arp_implementation arp_implementation.c -O2 -Wall -Wextra

# Compile with debugging symbols
gcc -g -o arp_implementation arp_implementation.c -Wall -Wextra
```

### Rust Version

```bash
# Build in debug mode
cargo build

# Build in release mode (optimized)
cargo build --release

# Run directly
cargo run

# Run tests
cargo test

# Run with verbose output
cargo run --verbose
```

## Usage

### C/C++ Implementation

**Basic Usage:**
```bash
# Must run with root privileges
sudo ./arp_implementation
```

**Modify for Your Network:**

Edit the source code to match your network configuration:

```c
// Change these values in main()
const char *interface = "eth0";        // Your network interface
const char *src_ip = "192.168.1.10";   // Your IP address
const char *target_ip = "192.168.1.1"; // Target IP to query
```

**Example Output:**
```
ARP Cache Contents:
IP Address      MAC Address       Type      
==================================================
192.168.1.1     aa:bb:cc:dd:ee:ff Static    
192.168.1.100   00:11:22:33:44:55 Dynamic   

--- ARP Packet ---
Operation: Request
Sender MAC: 00:11:22:33:44:55
Sender IP: 192.168.1.10
Target MAC: 00:00:00:00:00:00
Target IP: 192.168.1.1
```

### Rust Implementation

**Basic Usage:**
```bash
# Run the demo
cargo run

# Run in release mode
cargo run --release

# Run specific tests
cargo test test_arp_packet_serialization
cargo test test_cache_operations
```

**Example Output:**
```
=== ARP Protocol Implementation Demo ===

--- Demo 1: ARP Request ---
ARP Request
  Sender: 192.168.1.10 (00:11:22:33:44:55)
  Target: 192.168.1.100 (00:00:00:00:00:00)

Serialized to 28 bytes
Deserialized successfully

--- Demo 5: ARP Cache ---

ARP Cache Contents:
IP Address      MAC Address       Type      
==================================================
192.168.1.1     aa:bb:cc:dd:ee:ff Static    

--- Demo 7: ARP Spoof Detection ---
⚠️  ARP Spoof detected!
   IP: 192.168.1.1
   Known MAC: aa:bb:cc:dd:ee:ff
   Received MAC: ff:ff:ff:ff:ff:ff

=== Demo Complete ===
```

## Code Examples

### Creating an ARP Request (C)

```c
uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
struct arp_packet request;

create_arp_request(&request, 
                   "192.168.1.10",  // Source IP
                   src_mac,         // Source MAC
                   "192.168.1.1");  // Target IP

send_arp_packet("eth0", &request);
```

### Creating an ARP Request (Rust)

```rust
let local_mac = MacAddress::new([0x00, 0x11, 0x22, 0x33, 0x44, 0x55]);
let local_ip = "192.168.1.10".parse().unwrap();
let target_ip = "192.168.1.1".parse().unwrap();

let request = ArpPacket::new_request(local_mac, local_ip, target_ip);
println!("{}", request);
```

### Managing ARP Cache (C)

```c
arp_cache_t *cache = arp_cache_init(300); // 5 minute timeout

// Add entry
uint8_t mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
struct in_addr ip;
inet_pton(AF_INET, "192.168.1.1", &ip);
arp_cache_add(cache, ip.s_addr, mac, 1); // Static entry

// Lookup
uint8_t mac_out[6];
if (arp_cache_lookup(cache, ip.s_addr, mac_out)) {
    printf("Found MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac_out[0], mac_out[1], mac_out[2],
           mac_out[3], mac_out[4], mac_out[5]);
}

// Cleanup expired entries
arp_cache_cleanup(cache);
```

### Managing ARP Cache (Rust)

```rust
let mut cache = ArpCache::new(300); // 5 minute timeout

// Add entry
let mac = MacAddress::new([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]);
let ip = "192.168.1.1".parse().unwrap();
cache.add(ip, mac, true); // Static entry

// Lookup
if let Some(mac) = cache.lookup(&ip) {
    println!("Found MAC: {}", mac);
}

// Cleanup expired entries
cache.cleanup();
```

### Gratuitous ARP (C)

```c
uint8_t my_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
struct arp_packet garp;

create_gratuitous_arp(&garp, "192.168.1.10", my_mac);
send_arp_packet("eth0", &garp);
```

### Gratuitous ARP (Rust)

```rust
let my_mac = MacAddress::new([0x00, 0x11, 0x22, 0x33, 0x44, 0x55]);
let my_ip = "192.168.1.10".parse().unwrap();

let garp = ArpPacket::new_gratuitous(my_mac, my_ip);
println!("Gratuitous: {}", garp.is_gratuitous());
```

## Testing Your Implementation

### 1. View System ARP Cache

```bash
# Linux
ip neighbor show
arp -an

# Show specific interface
ip neighbor show dev eth0
```

### 2. Capture ARP Traffic

```bash
# Using tcpdump (requires root)
sudo tcpdump -i eth0 -n arp

# More verbose output
sudo tcpdump -i eth0 -n -vv arp

# Save to file
sudo tcpdump -i eth0 -n arp -w arp_capture.pcap
```

### 3. Send Test ARP Requests

```bash
# Using arping
sudo arping -I eth0 192.168.1.1

# Send gratuitous ARP
sudo arping -A -I eth0 $(hostname -I | awk '{print $1}')

# Specify source MAC (spoofing test)
sudo arping -I eth0 -s 192.168.1.100 192.168.1.1
```

### 4. Monitor for ARP Spoofing

```bash
# Install arpwatch
sudo apt-get install arpwatch

# Start monitoring
sudo arpwatch -i eth0

# Check logs
sudo tail -f /var/log/syslog | grep arp
```

## Security Considerations

⚠️ **Important Security Notes:**

1. **Raw Socket Access**: Both implementations require elevated privileges
   - Only run on trusted systems
   - Validate input carefully
   - Consider using capabilities instead of full root

2. **ARP Spoofing**: The code can be used for network attacks
   - Use only for legitimate testing
   - Implement proper authentication
   - Monitor for abuse

3. **Rate Limiting**: Production systems should implement:
   - Request rate limiting
   - Cache size limits
   - Anomaly detection

4. **Validation**: Always validate:
   - Source addresses
   - Packet sizes
   - Cache entries

## Performance Tuning

### Cache Timeout Recommendations

| Network Type | Timeout | Reason |
|-------------|---------|--------|
| Home/Small Office | 300s (5min) | Low change rate |
| Enterprise | 120s (2min) | Moderate change |
| Data Center | 60s (1min) | High mobility |
| Wi-Fi/Mobile | 30s | Frequent changes |

### Memory Considerations

```c
// Adjust cache size based on network
#define MAX_CACHE_ENTRIES 1024

// For large networks
#define MAX_CACHE_ENTRIES 4096

// For embedded systems
#define MAX_CACHE_ENTRIES 64
```

## Common Issues and Solutions

### Issue: "Operation not permitted"

**Solution:** Run with root privileges
```bash
sudo ./arp_implementation
# or
sudo cargo run
```

### Issue: "No such device"

**Solution:** Verify interface name
```bash
ip link show
# Use the correct interface name (eth0, ens33, wlan0, etc.)
```

### Issue: ARP requests not seen

**Solutions:**
1. Check firewall rules
   ```bash
   sudo iptables -L -n
   ```

2. Verify interface is up
   ```bash
   ip link set eth0 up
   ```

3. Check for network filtering
   ```bash
   sudo ethtool -k eth0 | grep rx-vlan
   ```

### Issue: Rust compilation errors

**Solutions:**
1. Update Rust toolchain
   ```bash
   rustup update stable
   ```

2. Clean build directory
   ```bash
   cargo clean
   cargo build
   ```

## Advanced Usage

### Integration with Packet Capture

```c
// Combine with libpcap for packet capture
#include <pcap.h>

pcap_t *handle = pcap_open_live("eth0", BUFSIZ, 1, 1000, errbuf);
pcap_loop(handle, -1, process_packet, NULL);
```

### Async Rust Version

```rust
// Use tokio for async operations
use tokio::net::UdpSocket;

#[tokio::main]
async fn main() {
    let mut handler = ArpHandler::new(local_ip, local_mac, 300);
    // Async packet processing
}
```

## Contributing

Contributions are welcome! Areas for improvement:

- [ ] IPv6 support (NDP)
- [ ] Windows support
- [ ] More comprehensive tests
- [ ] Benchmark suite
- [ ] GUI interface
- [ ] Integration with network namespaces

## License

These implementations are provided for educational purposes. Use responsibly and in accordance with applicable laws and regulations.

## References

- [RFC 826 - ARP](https://tools.ietf.org/html/rfc826)
- [RFC 5227 - IPv4 ACD](https://tools.ietf.org/html/rfc5227)
- Linux kernel ARP implementation: `net/ipv4/arp.c`
- [TCP/IP Illustrated, Volume 1](https://www.amazon.com/TCP-Illustrated-Volume-Implementation/dp/0201633469)

## Support

For questions or issues:
- Review the documentation: `ARP_Protocol_Documentation.md`
- Check system logs: `dmesg | grep -i arp`
- Examine network traces: `tcpdump -i any arp`

---

**⚡ Quick Start:**

```bash
# C Version
gcc -o arp_implementation arp_implementation.c
sudo ./arp_implementation

# Rust Version
cargo build --release
cargo run --release
```

**📚 Learn More:** Read `ARP_Protocol_Documentation.md` for detailed protocol information.
