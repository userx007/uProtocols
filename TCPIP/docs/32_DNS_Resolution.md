# DNS Resolution

## Overview

DNS (Domain Name System) resolution is the process of converting human-readable domain names (like `www.example.com`) into IP addresses (like `192.0.2.1`) that computers use to communicate. This is a fundamental operation in network programming, as nearly all network connections start with resolving a hostname to an IP address.

Modern applications use DNS resolution to:
- Translate domain names to IPv4 and IPv6 addresses
- Discover service endpoints (SRV records)
- Verify email servers (MX records)
- Implement load balancing and failover
- Support both synchronous and asynchronous I/O patterns

## Core Concepts

### Resolution Methods

**getaddrinfo (Modern Standard)**
- Protocol-independent interface supporting both IPv4 and IPv6
- Returns multiple addresses for redundancy and load balancing
- Handles service name to port number translation
- Configurable through hints structure

**Legacy Functions**
- `gethostbyname` - deprecated, IPv4 only
- `gethostbyname2` - deprecated, supports IPv6
- Not thread-safe and less flexible

**Custom DNS Queries**
- Direct UDP/TCP queries to DNS servers
- Full control over query types (A, AAAA, MX, TXT, etc.)
- Useful for specialized applications

**Asynchronous Resolution**
- Non-blocking operations for high-performance servers
- Event-driven or callback-based patterns
- Essential for concurrent request handling

## C/C++ Examples

### Basic DNS Resolution with getaddrinfo

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

int resolve_hostname(const char *hostname, const char *service) {
    struct addrinfo hints, *result, *rp;
    int status;
    char ipstr[INET6_ADDRSTRLEN];
    
    // Initialize hints structure
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP socket
    hints.ai_flags = AI_CANONNAME;    // Get canonical name
    
    // Perform resolution
    status = getaddrinfo(hostname, service, &hints, &result);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }
    
    printf("Resolved '%s':\n", hostname);
    if (result->ai_canonname) {
        printf("Canonical name: %s\n", result->ai_canonname);
    }
    
    // Iterate through all returned addresses
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        void *addr;
        const char *ipver;
        
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        } else {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }
        
        inet_ntop(rp->ai_family, addr, ipstr, sizeof(ipstr));
        printf("  %s: %s\n", ipver, ipstr);
    }
    
    freeaddrinfo(result);
    return 0;
}

int main() {
    resolve_hostname("www.google.com", "80");
    resolve_hostname("www.example.com", "https");
    return 0;
}
```

### Custom DNS Query (Raw UDP)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// DNS header structure
struct dns_header {
    unsigned short id;           // Query ID
    unsigned short flags;        // Flags
    unsigned short qdcount;      // Question count
    unsigned short ancount;      // Answer count
    unsigned short nscount;      // Authority count
    unsigned short arcount;      // Additional count
};

// Convert domain name to DNS format (e.g., "www.example.com" -> 3www7example3com0)
void encode_dns_name(unsigned char *dns, const char *host) {
    int lock = 0;
    strcat((char*)host, ".");
    
    for (int i = 0; i < strlen((char*)host); i++) {
        if (host[i] == '.') {
            *dns++ = i - lock;
            for (; lock < i; lock++) {
                *dns++ = host[lock];
            }
            lock++;
        }
    }
    *dns++ = '\0';
}

int dns_query(const char *hostname, const char *dns_server) {
    int sockfd;
    unsigned char buf[512], *qname;
    struct dns_header *dns = NULL;
    struct sockaddr_in dest;
    
    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // Configure DNS server address
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr(dns_server);
    
    // Build DNS query
    dns = (struct dns_header *)&buf;
    dns->id = htons(getpid());
    dns->flags = htons(0x0100);  // Standard query, recursion desired
    dns->qdcount = htons(1);     // One question
    dns->ancount = 0;
    dns->nscount = 0;
    dns->arcount = 0;
    
    // Add question
    qname = (unsigned char*)&buf[sizeof(struct dns_header)];
    encode_dns_name(qname, hostname);
    
    // Add query type (A record) and class (IN)
    int qname_len = strlen((char*)qname) + 1;
    unsigned short *qtype = (unsigned short*)&qname[qname_len];
    *qtype = htons(1);  // Type A
    unsigned short *qclass = (unsigned short*)&qname[qname_len + 2];
    *qclass = htons(1); // Class IN
    
    int query_len = sizeof(struct dns_header) + qname_len + 4;
    
    // Send query
    if (sendto(sockfd, buf, query_len, 0, 
               (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        perror("sendto");
        close(sockfd);
        return -1;
    }
    
    // Receive response
    socklen_t len = sizeof(dest);
    int n = recvfrom(sockfd, buf, sizeof(buf), 0, 
                     (struct sockaddr*)&dest, &len);
    
    if (n < 0) {
        perror("recvfrom");
        close(sockfd);
        return -1;
    }
    
    printf("Received %d bytes from DNS server\n", n);
    printf("Answer count: %d\n", ntohs(dns->ancount));
    
    close(sockfd);
    return 0;
}

int main() {
    dns_query("www.example.com", "8.8.8.8");
    return 0;
}
```

### Asynchronous DNS Resolution (C++ with threading)

```cpp
#include <iostream>
#include <thread>
#include <future>
#include <vector>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

class AsyncResolver {
public:
    struct ResolveResult {
        std::string hostname;
        std::vector<std::string> addresses;
        bool success;
        std::string error;
    };
    
    static std::future<ResolveResult> resolve_async(const std::string& hostname) {
        return std::async(std::launch::async, [hostname]() {
            return resolve_sync(hostname);
        });
    }
    
private:
    static ResolveResult resolve_sync(const std::string& hostname) {
        ResolveResult result;
        result.hostname = hostname;
        result.success = false;
        
        struct addrinfo hints, *res, *p;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        
        int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
        if (status != 0) {
            result.error = gai_strerror(status);
            return result;
        }
        
        char ipstr[INET6_ADDRSTRLEN];
        for (p = res; p != nullptr; p = p->ai_next) {
            void *addr;
            
            if (p->ai_family == AF_INET) {
                addr = &((struct sockaddr_in*)p->ai_addr)->sin_addr;
            } else {
                addr = &((struct sockaddr_in6*)p->ai_addr)->sin6_addr;
            }
            
            inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));
            result.addresses.push_back(ipstr);
        }
        
        freeaddrinfo(res);
        result.success = true;
        return result;
    }
};

int main() {
    std::vector<std::string> hostnames = {
        "www.google.com",
        "www.github.com",
        "www.stackoverflow.com",
        "www.rust-lang.org"
    };
    
    std::vector<std::future<AsyncResolver::ResolveResult>> futures;
    
    // Launch async resolutions
    for (const auto& host : hostnames) {
        futures.push_back(AsyncResolver::resolve_async(host));
    }
    
    // Collect results
    for (auto& fut : futures) {
        auto result = fut.get();
        
        std::cout << "Hostname: " << result.hostname << std::endl;
        if (result.success) {
            std::cout << "Addresses:" << std::endl;
            for (const auto& addr : result.addresses) {
                std::cout << "  " << addr << std::endl;
            }
        } else {
            std::cout << "Error: " << result.error << std::endl;
        }
        std::cout << std::endl;
    }
    
    return 0;
}
```

## Rust Examples

### Basic DNS Resolution with Standard Library

```rust
use std::net::{ToSocketAddrs, IpAddr};
use std::io;

fn resolve_hostname(hostname: &str, port: u16) -> io::Result<()> {
    let address = format!("{}:{}", hostname, port);
    
    println!("Resolving '{}':", hostname);
    
    // ToSocketAddrs trait performs DNS resolution
    match address.to_socket_addrs() {
        Ok(addrs) => {
            for addr in addrs {
                match addr.ip() {
                    IpAddr::V4(ipv4) => println!("  IPv4: {}", ipv4),
                    IpAddr::V6(ipv6) => println!("  IPv6: {}", ipv6),
                }
            }
            Ok(())
        }
        Err(e) => {
            eprintln!("Resolution failed: {}", e);
            Err(e)
        }
    }
}

fn main() -> io::Result<()> {
    resolve_hostname("www.google.com", 80)?;
    resolve_hostname("www.example.com", 443)?;
    Ok(())
}
```

### Advanced DNS with trust-dns-resolver

```rust
use trust_dns_resolver::config::*;
use trust_dns_resolver::Resolver;
use trust_dns_resolver::proto::rr::RecordType;

fn advanced_dns_lookup() -> Result<(), Box<dyn std::error::Error>> {
    // Create resolver with system configuration
    let resolver = Resolver::new(
        ResolverConfig::default(),
        ResolverOpts::default()
    )?;
    
    let hostname = "www.google.com";
    
    // A record lookup (IPv4)
    println!("A records for {}:", hostname);
    let a_response = resolver.lookup_ip(hostname)?;
    for ip in a_response.iter() {
        println!("  {}", ip);
    }
    
    // MX record lookup
    println!("\nMX records for google.com:");
    let mx_response = resolver.lookup(
        "google.com",
        RecordType::MX
    )?;
    
    for record in mx_response.iter() {
        println!("  {}", record);
    }
    
    // TXT record lookup
    println!("\nTXT records for google.com:");
    let txt_response = resolver.lookup(
        "google.com",
        RecordType::TXT
    )?;
    
    for record in txt_response.iter() {
        println!("  {}", record);
    }
    
    Ok(())
}

fn main() {
    if let Err(e) = advanced_dns_lookup() {
        eprintln!("DNS lookup failed: {}", e);
    }
}
```

### Asynchronous DNS Resolution with Tokio

```rust
use tokio::net::lookup_host;
use std::net::IpAddr;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let hostnames = vec![
        "www.google.com:80",
        "www.github.com:443",
        "www.rust-lang.org:443",
    ];
    
    let mut handles = vec![];
    
    // Spawn async tasks for each resolution
    for hostname in hostnames {
        let handle = tokio::spawn(async move {
            resolve_async(hostname).await
        });
        handles.push(handle);
    }
    
    // Wait for all resolutions to complete
    for handle in handles {
        handle.await??;
    }
    
    Ok(())
}

async fn resolve_async(address: &str) -> Result<(), Box<dyn std::error::Error>> {
    println!("Resolving '{}':", address);
    
    let addrs = lookup_host(address).await?;
    
    for addr in addrs {
        match addr.ip() {
            IpAddr::V4(ipv4) => println!("  IPv4: {}", ipv4),
            IpAddr::V6(ipv6) => println!("  IPv6: {}", ipv6),
        }
    }
    
    println!();
    Ok(())
}
```

### Custom DNS Query with trust-dns-client

```rust
use trust_dns_client::client::{Client, SyncClient};
use trust_dns_client::udp::UdpClientConnection;
use trust_dns_client::op::DnsResponse;
use trust_dns_client::rr::{DNSClass, Name, RData, RecordType};
use std::str::FromStr;
use std::net::SocketAddr;

fn custom_dns_query() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to Google DNS (8.8.8.8)
    let address = SocketAddr::from(([8, 8, 8, 8], 53));
    let conn = UdpClientConnection::new(address)?;
    let client = SyncClient::new(conn);
    
    // Create query for A record
    let name = Name::from_str("www.example.com.")?;
    
    let response: DnsResponse = client.query(
        &name,
        DNSClass::IN,
        RecordType::A
    )?;
    
    println!("Query for {}:", name);
    println!("Response code: {:?}", response.response_code());
    
    // Process answers
    for answer in response.answers() {
        if let Some(RData::A(addr)) = answer.data() {
            println!("  A record: {}", addr);
        }
    }
    
    // Query for AAAA record (IPv6)
    let response_aaaa: DnsResponse = client.query(
        &name,
        DNSClass::IN,
        RecordType::AAAA
    )?;
    
    for answer in response_aaaa.answers() {
        if let Some(RData::AAAA(addr)) = answer.data() {
            println!("  AAAA record: {}", addr);
        }
    }
    
    Ok(())
}

fn main() {
    if let Err(e) = custom_dns_query() {
        eprintln!("DNS query failed: {}", e);
    }
}
```

## Summary

DNS resolution is a critical component of network programming that translates human-readable hostnames into machine-usable IP addresses. The modern `getaddrinfo` function provides a protocol-independent, thread-safe interface that supports both IPv4 and IPv6, making it the preferred choice for most applications.

**Key takeaways:**

- **getaddrinfo** is the standard modern API for DNS resolution in C/C++, offering flexibility and protocol independence
- **Asynchronous resolution** is essential for high-performance applications to avoid blocking on DNS queries, which can take hundreds of milliseconds
- **Custom DNS queries** give fine-grained control over record types (A, AAAA, MX, TXT, SRV) for specialized applications
- **Rust** provides excellent DNS support through both the standard library's `ToSocketAddrs` trait and third-party crates like `trust-dns-resolver` for advanced features
- **Error handling** is crucial as DNS resolution can fail due to network issues, misconfiguration, or non-existent domains
- **Caching** DNS results can significantly improve performance, though TTL values must be respected

Whether building a simple client application or a high-performance server handling thousands of concurrent connections, understanding DNS resolution patterns and choosing the appropriate resolution strategy is fundamental to robust network programming.