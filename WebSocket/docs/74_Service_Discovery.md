# WebSocket Service Discovery in Distributed Systems

## Overview

Service Discovery is a critical component in distributed systems that enables automatic detection and location of WebSocket endpoints without hardcoding addresses. As microservices scale horizontally and instances come and go dynamically, service discovery mechanisms allow clients to find available WebSocket servers and maintain resilient connections in cloud-native architectures.

## Core Concepts

### What is Service Discovery?

Service discovery solves the problem of locating network services in dynamic environments where:
- Service instances are created and destroyed automatically (auto-scaling)
- IP addresses and ports change frequently
- Services may be deployed across multiple data centers
- Health checks determine service availability
- Load balancing requires awareness of all available instances

### Key Components

1. **Service Registry**: Central database storing service locations and metadata
2. **Registration**: Services announce their availability and endpoints
3. **Discovery**: Clients query the registry to find service endpoints
4. **Health Checking**: Continuous monitoring of service health
5. **Deregistration**: Removal of unavailable services from the registry

### Discovery Patterns

**Client-Side Discovery**: Client queries the registry and chooses an instance
**Server-Side Discovery**: Load balancer queries the registry on behalf of clients

## C/C++ Implementation

### Using Consul for Service Discovery

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

// Structure to hold service information
typedef struct {
    char service_id[64];
    char service_name[64];
    char address[256];
    int port;
    char ws_endpoint[512];
} ServiceInfo;

// Structure for HTTP response
typedef struct {
    char *data;
    size_t size;
} HTTPResponse;

// Callback for CURL to write response data
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    HTTPResponse *mem = (HTTPResponse *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not enough memory\n");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

// Register WebSocket service with Consul
int register_websocket_service(const char *consul_url, 
                               const char *service_name,
                               const char *service_id,
                               const char *address,
                               int port) {
    CURL *curl;
    CURLcode res;
    int success = 0;
    
    // Build registration URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/agent/service/register", consul_url);
    
    // Create registration JSON
    char json_data[1024];
    snprintf(json_data, sizeof(json_data),
        "{"
        "  \"ID\": \"%s\","
        "  \"Name\": \"%s\","
        "  \"Address\": \"%s\","
        "  \"Port\": %d,"
        "  \"Tags\": [\"websocket\", \"v1\"],"
        "  \"Check\": {"
        "    \"HTTP\": \"http://%s:%d/health\","
        "    \"Interval\": \"10s\","
        "    \"Timeout\": \"5s\""
        "  }"
        "}",
        service_id, service_name, address, port, address, port
    );
    
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
        
        res = curl_easy_perform(curl);
        
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            success = (response_code == 200);
            printf("Service registered with response code: %ld\n", response_code);
        } else {
            fprintf(stderr, "Registration failed: %s\n", curl_easy_strerror(res));
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    return success;
}

// Discover WebSocket services from Consul
int discover_websocket_services(const char *consul_url,
                                const char *service_name,
                                ServiceInfo **services,
                                int *count) {
    CURL *curl;
    CURLcode res;
    HTTPResponse response = {NULL, 0};
    int success = 0;
    
    // Build discovery URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/health/service/%s?passing", 
             consul_url, service_name);
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
        
        res = curl_easy_perform(curl);
        
        if (res == CURLE_OK && response.data) {
            // Parse JSON response
            struct json_object *parsed_json = json_tokener_parse(response.data);
            
            if (parsed_json) {
                int array_len = json_object_array_length(parsed_json);
                *services = malloc(sizeof(ServiceInfo) * array_len);
                *count = 0;
                
                for (int i = 0; i < array_len; i++) {
                    struct json_object *item = json_object_array_get_idx(parsed_json, i);
                    struct json_object *service_obj, *address_obj, *port_obj, *id_obj;
                    
                    if (json_object_object_get_ex(item, "Service", &service_obj)) {
                        json_object_object_get_ex(service_obj, "ID", &id_obj);
                        json_object_object_get_ex(service_obj, "Address", &address_obj);
                        json_object_object_get_ex(service_obj, "Port", &port_obj);
                        
                        const char *id = json_object_get_string(id_obj);
                        const char *addr = json_object_get_string(address_obj);
                        int port = json_object_get_int(port_obj);
                        
                        // Fill service info
                        strncpy((*services)[*count].service_id, id, 63);
                        strncpy((*services)[*count].service_name, service_name, 63);
                        strncpy((*services)[*count].address, addr, 255);
                        (*services)[*count].port = port;
                        
                        // Build WebSocket endpoint
                        snprintf((*services)[*count].ws_endpoint, 511, 
                                "ws://%s:%d/ws", addr, port);
                        
                        (*count)++;
                    }
                }
                
                json_object_put(parsed_json);
                success = 1;
            }
        }
        
        curl_easy_cleanup(curl);
    }
    
    if (response.data) {
        free(response.data);
    }
    
    return success;
}

// Deregister service
int deregister_service(const char *consul_url, const char *service_id) {
    CURL *curl;
    CURLcode res;
    int success = 0;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/agent/service/deregister/%s", 
             consul_url, service_id);
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        
        res = curl_easy_perform(curl);
        
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            success = (response_code == 200);
        }
        
        curl_easy_cleanup(curl);
    }
    
    return success;
}

// Example usage
int main() {
    const char *consul_url = "http://localhost:8500";
    const char *service_name = "websocket-chat";
    const char *service_id = "ws-chat-1";
    
    // Register the service
    if (register_websocket_service(consul_url, service_name, service_id, 
                                   "192.168.1.100", 8080)) {
        printf("Service registered successfully\n");
        
        // Discover services
        ServiceInfo *services = NULL;
        int count = 0;
        
        if (discover_websocket_services(consul_url, service_name, 
                                       &services, &count)) {
            printf("Discovered %d services:\n", count);
            for (int i = 0; i < count; i++) {
                printf("  - %s: %s (ID: %s)\n", 
                       services[i].service_name,
                       services[i].ws_endpoint,
                       services[i].service_id);
            }
            free(services);
        }
        
        // Deregister on shutdown
        deregister_service(consul_url, service_id);
    }
    
    return 0;
}
```

### DNS-based Service Discovery (C++)

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

class DNSServiceDiscovery {
private:
    std::string service_domain;
    
public:
    struct ServiceEndpoint {
        std::string hostname;
        std::string ip_address;
        int port;
        std::string ws_url;
    };
    
    DNSServiceDiscovery(const std::string& domain) : service_domain(domain) {}
    
    // Discover services via DNS SRV records
    std::vector<ServiceEndpoint> discoverServices() {
        std::vector<ServiceEndpoint> endpoints;
        
        // Query SRV record: _ws._tcp.example.com
        std::string srv_query = "_ws._tcp." + service_domain;
        
        struct addrinfo hints, *results, *rp;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        
        // Get address info
        int status = getaddrinfo(service_domain.c_str(), "80", &hints, &results);
        
        if (status != 0) {
            std::cerr << "DNS lookup failed: " << gai_strerror(status) << std::endl;
            return endpoints;
        }
        
        // Iterate through results
        for (rp = results; rp != nullptr; rp = rp->ai_next) {
            ServiceEndpoint endpoint;
            
            char host[NI_MAXHOST];
            char ip[INET6_ADDRSTRLEN];
            
            // Get hostname
            if (getnameinfo(rp->ai_addr, rp->ai_addrlen, 
                           host, sizeof(host), nullptr, 0, 0) == 0) {
                endpoint.hostname = host;
            }
            
            // Get IP address
            void *addr;
            if (rp->ai_family == AF_INET) {
                struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
                addr = &(ipv4->sin_addr);
                endpoint.port = ntohs(ipv4->sin_port);
            } else {
                struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
                addr = &(ipv6->sin6_addr);
                endpoint.port = ntohs(ipv6->sin6_port);
            }
            
            inet_ntop(rp->ai_family, addr, ip, sizeof(ip));
            endpoint.ip_address = ip;
            
            // Build WebSocket URL
            endpoint.ws_url = "ws://" + endpoint.ip_address + 
                             ":" + std::to_string(endpoint.port) + "/ws";
            
            endpoints.push_back(endpoint);
        }
        
        freeaddrinfo(results);
        return endpoints;
    }
    
    // Simple round-robin load balancing
    ServiceEndpoint selectEndpoint(const std::vector<ServiceEndpoint>& endpoints) {
        static size_t current_index = 0;
        
        if (endpoints.empty()) {
            throw std::runtime_error("No endpoints available");
        }
        
        ServiceEndpoint selected = endpoints[current_index];
        current_index = (current_index + 1) % endpoints.size();
        
        return selected;
    }
};

// Example usage
int main() {
    DNSServiceDiscovery discovery("chat.example.com");
    
    auto endpoints = discovery.discoverServices();
    
    std::cout << "Discovered " << endpoints.size() << " endpoints:" << std::endl;
    for (const auto& ep : endpoints) {
        std::cout << "  Host: " << ep.hostname << std::endl;
        std::cout << "  IP: " << ep.ip_address << std::endl;
        std::cout << "  Port: " << ep.port << std::endl;
        std::cout << "  WebSocket URL: " << ep.ws_url << std::endl;
        std::cout << std::endl;
    }
    
    // Select an endpoint
    if (!endpoints.empty()) {
        auto selected = discovery.selectEndpoint(endpoints);
        std::cout << "Selected endpoint: " << selected.ws_url << std::endl;
    }
    
    return 0;
}
```

## Rust Implementation

### Using Consul with Tokio

```rust
use serde::{Deserialize, Serialize};
use reqwest;
use std::error::Error;
use std::time::Duration;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ServiceInfo {
    pub service_id: String,
    pub service_name: String,
    pub address: String,
    pub port: u16,
    pub ws_endpoint: String,
}

#[derive(Serialize)]
struct ServiceRegistration {
    #[serde(rename = "ID")]
    id: String,
    #[serde(rename = "Name")]
    name: String,
    #[serde(rename = "Address")]
    address: String,
    #[serde(rename = "Port")]
    port: u16,
    #[serde(rename = "Tags")]
    tags: Vec<String>,
    #[serde(rename = "Check")]
    check: HealthCheck,
}

#[derive(Serialize)]
struct HealthCheck {
    #[serde(rename = "HTTP")]
    http: String,
    #[serde(rename = "Interval")]
    interval: String,
    #[serde(rename = "Timeout")]
    timeout: String,
}

#[derive(Deserialize)]
struct ConsulServiceResponse {
    #[serde(rename = "Service")]
    service: ConsulService,
}

#[derive(Deserialize)]
struct ConsulService {
    #[serde(rename = "ID")]
    id: String,
    #[serde(rename = "Service")]
    service: String,
    #[serde(rename = "Address")]
    address: String,
    #[serde(rename = "Port")]
    port: u16,
}

pub struct ConsulServiceDiscovery {
    consul_url: String,
    client: reqwest::Client,
}

impl ConsulServiceDiscovery {
    pub fn new(consul_url: String) -> Self {
        let client = reqwest::Client::builder()
            .timeout(Duration::from_secs(5))
            .build()
            .expect("Failed to create HTTP client");
        
        Self { consul_url, client }
    }
    
    // Register a WebSocket service
    pub async fn register_service(
        &self,
        service_name: &str,
        service_id: &str,
        address: &str,
        port: u16,
    ) -> Result<(), Box<dyn Error>> {
        let url = format!("{}/v1/agent/service/register", self.consul_url);
        
        let registration = ServiceRegistration {
            id: service_id.to_string(),
            name: service_name.to_string(),
            address: address.to_string(),
            port,
            tags: vec!["websocket".to_string(), "v1".to_string()],
            check: HealthCheck {
                http: format!("http://{}:{}/health", address, port),
                interval: "10s".to_string(),
                timeout: "5s".to_string(),
            },
        };
        
        let response = self.client
            .put(&url)
            .json(&registration)
            .send()
            .await?;
        
        if response.status().is_success() {
            println!("Service {} registered successfully", service_id);
            Ok(())
        } else {
            Err(format!("Registration failed with status: {}", response.status()).into())
        }
    }
    
    // Discover healthy WebSocket services
    pub async fn discover_services(
        &self,
        service_name: &str,
    ) -> Result<Vec<ServiceInfo>, Box<dyn Error>> {
        let url = format!(
            "{}/v1/health/service/{}?passing=true",
            self.consul_url, service_name
        );
        
        let response = self.client.get(&url).send().await?;
        
        if !response.status().is_success() {
            return Err(format!("Discovery failed with status: {}", response.status()).into());
        }
        
        let services: Vec<ConsulServiceResponse> = response.json().await?;
        
        let service_infos: Vec<ServiceInfo> = services
            .into_iter()
            .map(|s| {
                let ws_endpoint = format!("ws://{}:{}/ws", s.service.address, s.service.port);
                ServiceInfo {
                    service_id: s.service.id,
                    service_name: s.service.service,
                    address: s.service.address,
                    port: s.service.port,
                    ws_endpoint,
                }
            })
            .collect();
        
        Ok(service_infos)
    }
    
    // Deregister a service
    pub async fn deregister_service(&self, service_id: &str) -> Result<(), Box<dyn Error>> {
        let url = format!("{}/v1/agent/service/deregister/{}", self.consul_url, service_id);
        
        let response = self.client.put(&url).send().await?;
        
        if response.status().is_success() {
            println!("Service {} deregistered successfully", service_id);
            Ok(())
        } else {
            Err(format!("Deregistration failed with status: {}", response.status()).into())
        }
    }
}

// Load balancer with service discovery
pub struct LoadBalancedWebSocketClient {
    discovery: ConsulServiceDiscovery,
    service_name: String,
    current_index: std::sync::atomic::AtomicUsize,
}

impl LoadBalancedWebSocketClient {
    pub fn new(consul_url: String, service_name: String) -> Self {
        Self {
            discovery: ConsulServiceDiscovery::new(consul_url),
            service_name,
            current_index: std::sync::atomic::AtomicUsize::new(0),
        }
    }
    
    // Get next available endpoint (round-robin)
    pub async fn get_endpoint(&self) -> Result<ServiceInfo, Box<dyn Error>> {
        let services = self.discovery.discover_services(&self.service_name).await?;
        
        if services.is_empty() {
            return Err("No services available".into());
        }
        
        let index = self.current_index.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let selected = &services[index % services.len()];
        
        Ok(selected.clone())
    }
    
    // Connect with automatic failover
    pub async fn connect_with_retry(&self, max_retries: u32) -> Result<ServiceInfo, Box<dyn Error>> {
        for attempt in 0..max_retries {
            match self.get_endpoint().await {
                Ok(endpoint) => {
                    println!("Connecting to: {}", endpoint.ws_endpoint);
                    // Here you would actually establish the WebSocket connection
                    return Ok(endpoint);
                }
                Err(e) if attempt < max_retries - 1 => {
                    eprintln!("Connection attempt {} failed: {}", attempt + 1, e);
                    tokio::time::sleep(Duration::from_secs(2)).await;
                }
                Err(e) => return Err(e),
            }
        }
        
        Err("Max retries exceeded".into())
    }
}

// Example usage
#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let consul_url = "http://localhost:8500".to_string();
    let discovery = ConsulServiceDiscovery::new(consul_url.clone());
    
    // Register a service
    discovery.register_service(
        "websocket-chat",
        "ws-chat-1",
        "192.168.1.100",
        8080,
    ).await?;
    
    // Discover services
    let services = discovery.discover_services("websocket-chat").await?;
    println!("Discovered {} services:", services.len());
    for service in &services {
        println!("  - {}: {} (ID: {})", 
                 service.service_name, 
                 service.ws_endpoint, 
                 service.service_id);
    }
    
    // Use load-balanced client
    let lb_client = LoadBalancedWebSocketClient::new(
        consul_url.clone(),
        "websocket-chat".to_string(),
    );
    
    match lb_client.connect_with_retry(3).await {
        Ok(endpoint) => println!("Connected to: {}", endpoint.ws_endpoint),
        Err(e) => eprintln!("Failed to connect: {}", e),
    }
    
    // Cleanup
    discovery.deregister_service("ws-chat-1").await?;
    
    Ok(())
}
```

### Etcd-based Service Discovery

```rust
use etcd_client::{Client, PutOptions, GetOptions, WatchOptions};
use serde::{Deserialize, Serialize};
use std::error::Error;
use std::time::Duration;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct WebSocketService {
    pub id: String,
    pub address: String,
    pub port: u16,
    pub metadata: std::collections::HashMap<String, String>,
}

pub struct EtcdServiceRegistry {
    client: Client,
    service_prefix: String,
}

impl EtcdServiceRegistry {
    pub async fn new(endpoints: Vec<String>, service_prefix: String) -> Result<Self, Box<dyn Error>> {
        let client = Client::connect(endpoints, None).await?;
        Ok(Self { client, service_prefix })
    }
    
    // Register service with TTL (lease)
    pub async fn register_with_ttl(
        &mut self,
        service: &WebSocketService,
        ttl_seconds: i64,
    ) -> Result<(), Box<dyn Error>> {
        // Create a lease
        let lease = self.client.lease_grant(ttl_seconds, None).await?;
        let lease_id = lease.id();
        
        // Serialize service info
        let service_json = serde_json::to_string(service)?;
        let key = format!("{}/{}", self.service_prefix, service.id);
        
        // Put with lease
        let options = PutOptions::new().with_lease(lease_id);
        self.client.put(key.clone(), service_json, Some(options)).await?;
        
        println!("Service {} registered with lease {}", service.id, lease_id);
        
        // Keep-alive in background
        tokio::spawn(async move {
            // Note: In production, you'd want to handle this more robustly
        });
        
        Ok(())
    }
    
    // Discover all services
    pub async fn discover_all(&mut self) -> Result<Vec<WebSocketService>, Box<dyn Error>> {
        let options = GetOptions::new().with_prefix();
        let response = self.client.get(self.service_prefix.clone(), Some(options)).await?;
        
        let mut services = Vec::new();
        
        for kv in response.kvs() {
            if let Ok(value_str) = std::str::from_utf8(kv.value()) {
                if let Ok(service) = serde_json::from_str::<WebSocketService>(value_str) {
                    services.push(service);
                }
            }
        }
        
        Ok(services)
    }
    
    // Watch for service changes
    pub async fn watch_services<F>(&mut self, mut callback: F) -> Result<(), Box<dyn Error>>
    where
        F: FnMut(Vec<WebSocketService>) + Send + 'static,
    {
        let options = WatchOptions::new().with_prefix();
        let (_watcher, mut stream) = self.client.watch(
            self.service_prefix.clone(),
            Some(options),
        ).await?;
        
        while let Some(resp) = stream.message().await? {
            if resp.canceled() {
                break;
            }
            
            // Fetch current services on any change
            let services = self.discover_all().await?;
            callback(services);
        }
        
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let mut registry = EtcdServiceRegistry::new(
        vec!["localhost:2379".to_string()],
        "/services/websocket".to_string(),
    ).await?;
    
    // Register a service
    let service = WebSocketService {
        id: "ws-server-1".to_string(),
        address: "192.168.1.100".to_string(),
        port: 8080,
        metadata: [
            ("version".to_string(), "1.0".to_string()),
            ("region".to_string(), "us-west".to_string()),
        ].iter().cloned().collect(),
    };
    
    registry.register_with_ttl(&service, 30).await?;
    
    // Discover services
    let services = registry.discover_all().await?;
    println!("Found {} services", services.len());
    
    Ok(())
}
```

## Summary

**Service Discovery for WebSocket** endpoints enables dynamic, scalable distributed systems where services can automatically locate and connect to available WebSocket servers without hardcoded configuration.

**Key Takeaways:**
- **Essential for microservices** that scale dynamically across cloud infrastructure
- **Popular registries** include Consul, etcd, Eureka, and Zookeeper, plus DNS-based solutions
- **Client-side discovery** gives clients control over load balancing and failover strategies
- **Health checking** ensures only available services receive connections
- **TTL and heartbeats** automatically remove failed instances from the registry
- **Watch mechanisms** enable real-time updates when service topology changes

**Implementation considerations** include handling network partitions, implementing retry logic with exponential backoff, caching discovered endpoints for performance, supporting multiple data centers or regions, and integrating with service meshes like Istio or Linkerd for advanced traffic management.

Service discovery transforms static WebSocket architectures into resilient, elastic systems capable of handling modern cloud-native deployment patterns.