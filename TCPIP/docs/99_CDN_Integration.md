# CDN Integration: Content Delivery Networks, Caching Strategies, and Edge Servers

## Detailed Description

### What is CDN Integration?

A Content Delivery Network (CDN) is a geographically distributed network of proxy servers and data centers designed to deliver web content and services with high availability and performance. CDN integration involves implementing strategies to efficiently distribute, cache, and serve content from edge servers closest to end users, minimizing latency and reducing bandwidth costs.

### Core Concepts

**1. Edge Servers**
Edge servers are strategically placed servers located at various geographic points of presence (PoPs) closer to end users. They cache and serve content, reducing the round-trip time to origin servers.

**2. Caching Strategies**
- **Cache-Control Headers**: HTTP headers that dictate caching behavior
- **Time-to-Live (TTL)**: Duration for which content remains valid in cache
- **Cache Invalidation**: Purging or updating stale content
- **Cache Keys**: Identifiers used to store and retrieve cached content

**3. Content Distribution**
- **Pull-based CDN**: Content fetched from origin on first request
- **Push-based CDN**: Content proactively uploaded to edge servers
- **Hybrid Approach**: Combination of both strategies

**4. Request Routing**
- **DNS-based routing**: Using DNS to direct users to nearest edge server
- **Anycast routing**: Multiple servers share same IP address
- **Geographic routing**: Based on user's physical location

### Key Features in CDN Integration

1. **Origin Shield**: Additional caching layer between edge and origin
2. **Cache Warming**: Pre-populating cache before traffic spike
3. **Compression**: Gzip, Brotli compression for reduced bandwidth
4. **SSL/TLS Termination**: Handling encryption at edge servers
5. **Request Coalescing**: Combining multiple identical requests

---

## C/C++ Implementation

### Example 1: Basic HTTP Cache-Control Header Parser

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    int max_age;
    int s_maxage;
    int is_public;
    int is_private;
    int no_cache;
    int no_store;
    int must_revalidate;
} CacheControl;

// Parse Cache-Control header
CacheControl parse_cache_control(const char* header) {
    CacheControl cc = {0};
    char* header_copy = strdup(header);
    char* token = strtok(header_copy, ",");
    
    while (token != NULL) {
        // Trim leading whitespace
        while (*token == ' ') token++;
        
        if (strncmp(token, "max-age=", 8) == 0) {
            cc.max_age = atoi(token + 8);
        } else if (strncmp(token, "s-maxage=", 9) == 0) {
            cc.s_maxage = atoi(token + 9);
        } else if (strcmp(token, "public") == 0) {
            cc.is_public = 1;
        } else if (strcmp(token, "private") == 0) {
            cc.is_private = 1;
        } else if (strcmp(token, "no-cache") == 0) {
            cc.no_cache = 1;
        } else if (strcmp(token, "no-store") == 0) {
            cc.no_store = 1;
        } else if (strcmp(token, "must-revalidate") == 0) {
            cc.must_revalidate = 1;
        }
        
        token = strtok(NULL, ",");
    }
    
    free(header_copy);
    return cc;
}

// Check if cached content is still valid
int is_cache_valid(time_t cached_time, int max_age) {
    time_t current_time = time(NULL);
    return (current_time - cached_time) < max_age;
}

int main() {
    const char* header = "public, max-age=3600, s-maxage=7200, must-revalidate";
    CacheControl cc = parse_cache_control(header);
    
    printf("Cache-Control Analysis:\n");
    printf("  max-age: %d seconds\n", cc.max_age);
    printf("  s-maxage: %d seconds\n", cc.s_maxage);
    printf("  public: %s\n", cc.is_public ? "yes" : "no");
    printf("  must-revalidate: %s\n", cc.must_revalidate ? "yes" : "no");
    
    // Simulate cache validation
    time_t cached_time = time(NULL) - 1800; // Cached 30 minutes ago
    if (is_cache_valid(cached_time, cc.max_age)) {
        printf("\nCache is still valid!\n");
    } else {
        printf("\nCache expired, need to revalidate.\n");
    }
    
    return 0;
}
```

### Example 2: Simple CDN Edge Server Cache (C++)

```cpp
#include <iostream>
#include <unordered_map>
#include <string>
#include <chrono>
#include <memory>
#include <optional>

class CachedContent {
public:
    std::string content;
    std::chrono::system_clock::time_point cached_at;
    int ttl_seconds;
    std::string etag;
    
    CachedContent(const std::string& c, int ttl, const std::string& tag)
        : content(c), ttl_seconds(ttl), etag(tag) {
        cached_at = std::chrono::system_clock::now();
    }
    
    bool is_expired() const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - cached_at).count();
        return age >= ttl_seconds;
    }
    
    int get_age() const {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
            now - cached_at).count();
    }
};

class EdgeCache {
private:
    std::unordered_map<std::string, std::shared_ptr<CachedContent>> cache;
    size_t max_size;
    size_t current_size;
    
    std::string generate_cache_key(const std::string& url, 
                                   const std::string& query_params = "") {
        return url + "?" + query_params;
    }
    
public:
    EdgeCache(size_t max_sz = 1000) : max_size(max_sz), current_size(0) {}
    
    // Store content in cache
    void set(const std::string& url, const std::string& content, 
             int ttl, const std::string& etag = "") {
        std::string key = generate_cache_key(url);
        
        // Simple eviction: remove if cache is full
        if (current_size >= max_size && cache.find(key) == cache.end()) {
            // In production, use LRU or other sophisticated eviction
            cache.erase(cache.begin());
            current_size--;
        }
        
        auto cached = std::make_shared<CachedContent>(content, ttl, etag);
        cache[key] = cached;
        current_size++;
        
        std::cout << "Cached: " << key << " (TTL: " << ttl << "s)\n";
    }
    
    // Retrieve content from cache
    std::optional<std::string> get(const std::string& url) {
        std::string key = generate_cache_key(url);
        
        auto it = cache.find(key);
        if (it == cache.end()) {
            std::cout << "Cache MISS: " << url << "\n";
            return std::nullopt;
        }
        
        auto& cached = it->second;
        
        // Check if expired
        if (cached->is_expired()) {
            std::cout << "Cache EXPIRED: " << url << "\n";
            cache.erase(it);
            current_size--;
            return std::nullopt;
        }
        
        std::cout << "Cache HIT: " << url << " (Age: " 
                  << cached->get_age() << "s)\n";
        return cached->content;
    }
    
    // Invalidate (purge) cache entry
    bool invalidate(const std::string& url) {
        std::string key = generate_cache_key(url);
        auto it = cache.find(key);
        
        if (it != cache.end()) {
            cache.erase(it);
            current_size--;
            std::cout << "Invalidated: " << url << "\n";
            return true;
        }
        return false;
    }
    
    // Get cache statistics
    void print_stats() const {
        std::cout << "\n=== Cache Statistics ===\n";
        std::cout << "Entries: " << current_size << "/" << max_size << "\n";
        std::cout << "=======================\n\n";
    }
};

// Simulate origin server fetch
std::string fetch_from_origin(const std::string& url) {
    std::cout << "Fetching from ORIGIN: " << url << "\n";
    return "Content for " + url;
}

int main() {
    EdgeCache edge_cache(100);
    
    std::string url1 = "/images/logo.png";
    std::string url2 = "/api/users/123";
    
    // First request - cache miss, fetch from origin
    auto content1 = edge_cache.get(url1);
    if (!content1) {
        std::string origin_content = fetch_from_origin(url1);
        edge_cache.set(url1, origin_content, 3600, "etag-12345");
    }
    
    // Second request - cache hit
    auto content2 = edge_cache.get(url1);
    
    // Different URL - cache miss
    auto content3 = edge_cache.get(url2);
    if (!content3) {
        std::string origin_content = fetch_from_origin(url2);
        edge_cache.set(url2, origin_content, 60); // Short TTL for API
    }
    
    // Cache invalidation
    edge_cache.invalidate(url1);
    
    // After invalidation - cache miss again
    auto content4 = edge_cache.get(url1);
    
    edge_cache.print_stats();
    
    return 0;
}
```

---

## Rust Implementation

### Example 1: Cache-Control Header Parser with Strong Types

```rust
use std::collections::HashMap;
use std::time::{Duration, SystemTime};

#[derive(Debug, Clone, Default)]
pub struct CacheControl {
    pub max_age: Option<u64>,
    pub s_maxage: Option<u64>,
    pub public: bool,
    pub private: bool,
    pub no_cache: bool,
    pub no_store: bool,
    pub must_revalidate: bool,
    pub proxy_revalidate: bool,
}

impl CacheControl {
    pub fn parse(header: &str) -> Self {
        let mut cc = CacheControl::default();
        
        for directive in header.split(',') {
            let directive = directive.trim();
            
            if let Some((key, value)) = directive.split_once('=') {
                match key {
                    "max-age" => cc.max_age = value.parse().ok(),
                    "s-maxage" => cc.s_maxage = value.parse().ok(),
                    _ => {}
                }
            } else {
                match directive {
                    "public" => cc.public = true,
                    "private" => cc.private = true,
                    "no-cache" => cc.no_cache = true,
                    "no-store" => cc.no_store = true,
                    "must-revalidate" => cc.must_revalidate = true,
                    "proxy-revalidate" => cc.proxy_revalidate = true,
                    _ => {}
                }
            }
        }
        
        cc
    }
    
    pub fn is_cacheable(&self) -> bool {
        !self.no_store && !self.private
    }
    
    pub fn get_ttl(&self, is_shared_cache: bool) -> Option<Duration> {
        if is_shared_cache {
            self.s_maxage.or(self.max_age).map(Duration::from_secs)
        } else {
            self.max_age.map(Duration::from_secs)
        }
    }
}

fn main() {
    let header = "public, max-age=3600, s-maxage=7200, must-revalidate";
    let cc = CacheControl::parse(header);
    
    println!("Cache-Control Analysis:");
    println!("  max-age: {:?}", cc.max_age);
    println!("  s-maxage: {:?}", cc.s_maxage);
    println!("  public: {}", cc.public);
    println!("  must-revalidate: {}", cc.must_revalidate);
    println!("  is_cacheable: {}", cc.is_cacheable());
    
    if let Some(ttl) = cc.get_ttl(true) {
        println!("  TTL for shared cache: {:?}", ttl);
    }
}
```

### Example 2: Advanced Edge Cache with LRU Eviction

```rust
use std::collections::HashMap;
use std::hash::Hash;
use std::time::{Duration, SystemTime};

#[derive(Debug, Clone)]
pub struct CachedItem<T> {
    pub content: T,
    pub cached_at: SystemTime,
    pub ttl: Duration,
    pub etag: Option<String>,
    pub hits: u64,
}

impl<T> CachedItem<T> {
    pub fn new(content: T, ttl: Duration, etag: Option<String>) -> Self {
        Self {
            content,
            cached_at: SystemTime::now(),
            ttl,
            etag,
            hits: 0,
        }
    }
    
    pub fn is_expired(&self) -> bool {
        self.cached_at.elapsed().unwrap_or(Duration::ZERO) >= self.ttl
    }
    
    pub fn age(&self) -> Duration {
        self.cached_at.elapsed().unwrap_or(Duration::ZERO)
    }
}

#[derive(Debug)]
pub struct EdgeCache<K: Hash + Eq, V> {
    cache: HashMap<K, CachedItem<V>>,
    max_size: usize,
    hits: u64,
    misses: u64,
}

impl<K: Hash + Eq + Clone, V: Clone> EdgeCache<K, V> {
    pub fn new(max_size: usize) -> Self {
        Self {
            cache: HashMap::with_capacity(max_size),
            max_size,
            hits: 0,
            misses: 0,
        }
    }
    
    pub fn set(&mut self, key: K, value: V, ttl: Duration, etag: Option<String>) {
        // Simple eviction: remove first item if at capacity
        if self.cache.len() >= self.max_size && !self.cache.contains_key(&key) {
            // In production, implement proper LRU eviction
            if let Some(first_key) = self.cache.keys().next().cloned() {
                self.cache.remove(&first_key);
            }
        }
        
        let item = CachedItem::new(value, ttl, etag);
        self.cache.insert(key, item);
    }
    
    pub fn get(&mut self, key: &K) -> Option<V> {
        if let Some(item) = self.cache.get_mut(key) {
            if item.is_expired() {
                self.cache.remove(key);
                self.misses += 1;
                return None;
            }
            
            item.hits += 1;
            self.hits += 1;
            Some(item.content.clone())
        } else {
            self.misses += 1;
            None
        }
    }
    
    pub fn invalidate(&mut self, key: &K) -> bool {
        self.cache.remove(key).is_some()
    }
    
    pub fn invalidate_pattern(&mut self, predicate: impl Fn(&K) -> bool) {
        self.cache.retain(|k, _| !predicate(k));
    }
    
    pub fn hit_ratio(&self) -> f64 {
        let total = self.hits + self.misses;
        if total == 0 {
            0.0
        } else {
            self.hits as f64 / total as f64
        }
    }
    
    pub fn stats(&self) {
        println!("\n=== Cache Statistics ===");
        println!("Entries: {}/{}", self.cache.len(), self.max_size);
        println!("Hits: {}", self.hits);
        println!("Misses: {}", self.misses);
        println!("Hit Ratio: {:.2}%", self.hit_ratio() * 100.0);
        println!("=======================\n");
    }
}

// Simulate CDN request handling
fn handle_request(cache: &mut EdgeCache<String, String>, url: &str) -> String {
    println!("Request: {}", url);
    
    if let Some(content) = cache.get(&url.to_string()) {
        println!("  -> Cache HIT");
        content
    } else {
        println!("  -> Cache MISS, fetching from origin");
        let content = format!("Content for {}", url);
        
        // Determine TTL based on content type
        let ttl = if url.contains("/api/") {
            Duration::from_secs(60) // API: 1 minute
        } else if url.contains("/static/") {
            Duration::from_secs(86400) // Static: 1 day
        } else {
            Duration::from_secs(3600) // Default: 1 hour
        };
        
        cache.set(url.to_string(), content.clone(), ttl, None);
        content
    }
}

fn main() {
    let mut cache = EdgeCache::new(100);
    
    // Simulate requests
    handle_request(&mut cache, "/static/logo.png");
    handle_request(&mut cache, "/static/logo.png"); // Hit
    handle_request(&mut cache, "/api/users/123");
    handle_request(&mut cache, "/api/users/123"); // Hit
    handle_request(&mut cache, "/page/home");
    
    // Invalidate API cache
    println!("\nInvalidating all /api/ entries...");
    cache.invalidate_pattern(|key| key.contains("/api/"));
    
    handle_request(&mut cache, "/api/users/123"); // Miss after invalidation
    
    cache.stats();
}
```

### Example 3: CDN Origin Shield Implementation

```rust
use std::sync::{Arc, Mutex};
use std::collections::HashMap;
use std::time::Duration;
use tokio::time::sleep;

#[derive(Clone)]
struct OriginShield {
    pending_requests: Arc<Mutex<HashMap<String, Arc<Mutex<Option<String>>>>>>,
}

impl OriginShield {
    fn new() -> Self {
        Self {
            pending_requests: Arc::new(Mutex::new(HashMap::new())),
        }
    }
    
    // Request coalescing: multiple identical requests share single origin fetch
    async fn fetch_with_coalescing(&self, url: &str) -> String {
        let url = url.to_string();
        
        // Check if request is already pending
        let shared_result = {
            let mut pending = self.pending_requests.lock().unwrap();
            
            if let Some(result) = pending.get(&url) {
                println!("Coalescing request for: {}", url);
                result.clone()
            } else {
                // Create new pending request
                let result = Arc::new(Mutex::new(None));
                pending.insert(url.clone(), result.clone());
                
                // Spawn fetch task
                let url_clone = url.clone();
                let result_clone = result.clone();
                let pending_clone = self.pending_requests.clone();
                
                tokio::spawn(async move {
                    println!("Fetching from origin: {}", url_clone);
                    sleep(Duration::from_millis(100)).await; // Simulate network delay
                    
                    let content = format!("Content for {}", url_clone);
                    
                    // Store result
                    *result_clone.lock().unwrap() = Some(content);
                    
                    // Remove from pending
                    pending_clone.lock().unwrap().remove(&url_clone);
                });
                
                result
            }
        };
        
        // Wait for result
        loop {
            if let Some(content) = shared_result.lock().unwrap().clone() {
                return content;
            }
            sleep(Duration::from_millis(10)).await;
        }
    }
}

#[tokio::main]
async fn main() {
    let shield = OriginShield::new();
    
    // Simulate multiple concurrent requests for same resource
    let mut handles = vec![];
    
    for i in 0..5 {
        let shield_clone = shield.clone();
        let handle = tokio::spawn(async move {
            let content = shield_clone.fetch_with_coalescing("/api/data").await;
            println!("Request {} received: {}", i, content);
        });
        handles.push(handle);
    }
    
    for handle in handles {
        handle.await.unwrap();
    }
}
```

---

## Summary

**CDN Integration** is a critical component of modern web infrastructure that dramatically improves application performance, reliability, and scalability by strategically distributing content closer to end users.

### Key Takeaways:

1. **Performance Benefits**: CDNs reduce latency by serving content from geographically distributed edge servers, minimizing round-trip times to origin servers.

2. **Caching Strategies**: Effective cache management through proper TTL configuration, cache-control headers, and invalidation mechanisms is essential for balancing freshness and performance.

3. **Implementation Considerations**:
   - **C/C++**: Offers low-level control and high performance, suitable for building CDN infrastructure components
   - **Rust**: Provides memory safety and concurrency without garbage collection, excellent for building reliable edge servers

4. **Core Features**:
   - Request coalescing to prevent thundering herd problems
   - Origin shield to reduce load on origin servers
   - LRU or other eviction policies for cache management
   - Support for conditional requests (ETags, Last-Modified)

5. **Best Practices**:
   - Set appropriate TTL values based on content type
   - Implement cache warming for predictable traffic spikes
   - Use cache invalidation judiciously to maintain data freshness
   - Monitor cache hit ratios and optimize accordingly
   - Consider shared vs. private caching for different content types

CDN integration transforms the user experience by ensuring fast, reliable content delivery while reducing infrastructure costs and origin server load.