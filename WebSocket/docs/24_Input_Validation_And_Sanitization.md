# Input Validation and Sanitization in WebSocket Applications

## Overview

Input validation and sanitization are critical security practices for WebSocket applications. Unlike HTTP requests that are stateless and short-lived, WebSocket connections maintain persistent bidirectional channels where malicious data can continuously flow. Without proper validation, applications become vulnerable to injection attacks, buffer overflows, denial-of-service attacks, and data corruption.

## Core Concepts

**Input Validation** verifies that incoming data conforms to expected formats, types, ranges, and constraints before processing. **Sanitization** cleans or escapes data to remove or neutralize potentially harmful content.

### Common WebSocket Vulnerabilities

WebSocket applications face unique security challenges:

- **Message injection attacks**: Malicious payloads embedded in WebSocket frames
- **Cross-Site WebSocket Hijacking (CSWSH)**: Unauthorized connections from malicious sites
- **Protocol-level attacks**: Malformed frames, excessive fragmentation, or compression bombs
- **Application-level attacks**: SQL injection, command injection, XSS through WebSocket messages
- **Resource exhaustion**: Oversized messages or rapid message flooding

### Defense Strategy

A robust defense employs multiple layers:

1. **Origin validation** during handshake
2. **Message size limits** and rate limiting
3. **Format validation** (JSON schema, protocol buffers, etc.)
4. **Content sanitization** (HTML escaping, SQL parameterization)
5. **Type checking** and bounds validation
6. **Business logic validation**

## C/C++ Implementation

C/C++ provides low-level control but requires careful memory management and bounds checking.

```cpp
#include <iostream>
#include <string>
#include <regex>
#include <memory>
#include <cstring>
#include <algorithm>
#include <cctype>

// Maximum message sizes to prevent DoS
constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB
constexpr size_t MAX_USERNAME_LENGTH = 50;
constexpr size_t MAX_CHANNEL_LENGTH = 100;

class InputValidator {
public:
    // Validate message size
    static bool validateMessageSize(size_t size) {
        return size > 0 && size <= MAX_MESSAGE_SIZE;
    }
    
    // Validate UTF-8 encoding
    static bool isValidUTF8(const std::string& str) {
        const unsigned char* bytes = 
            reinterpret_cast<const unsigned char*>(str.c_str());
        size_t len = str.length();
        
        for (size_t i = 0; i < len; ) {
            unsigned char c = bytes[i];
            
            if (c <= 0x7F) {
                i++; // ASCII
            } else if ((c & 0xE0) == 0xC0) {
                if (i + 1 >= len || (bytes[i+1] & 0xC0) != 0x80) 
                    return false;
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                if (i + 2 >= len || 
                    (bytes[i+1] & 0xC0) != 0x80 || 
                    (bytes[i+2] & 0xC0) != 0x80) 
                    return false;
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                if (i + 3 >= len || 
                    (bytes[i+1] & 0xC0) != 0x80 || 
                    (bytes[i+2] & 0xC0) != 0x80 || 
                    (bytes[i+3] & 0xC0) != 0x80) 
                    return false;
                i += 4;
            } else {
                return false;
            }
        }
        return true;
    }
    
    // Sanitize username - alphanumeric and underscores only
    static std::string sanitizeUsername(const std::string& username) {
        if (username.length() > MAX_USERNAME_LENGTH) {
            return "";
        }
        
        std::string sanitized;
        sanitized.reserve(username.length());
        
        for (char c : username) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                sanitized += c;
            }
        }
        
        return sanitized;
    }
    
    // Validate and sanitize channel name
    static std::string sanitizeChannel(const std::string& channel) {
        if (channel.length() > MAX_CHANNEL_LENGTH || channel.empty()) {
            return "";
        }
        
        // Channel must start with # and contain valid chars
        if (channel[0] != '#') {
            return "";
        }
        
        std::regex channelRegex("^#[a-zA-Z0-9_-]+$");
        if (!std::regex_match(channel, channelRegex)) {
            return "";
        }
        
        return channel;
    }
    
    // HTML escape to prevent XSS
    static std::string htmlEscape(const std::string& input) {
        std::string escaped;
        escaped.reserve(input.length() * 1.2); // Reserve extra space
        
        for (char c : input) {
            switch (c) {
                case '&':  escaped += "&amp;"; break;
                case '<':  escaped += "&lt;"; break;
                case '>':  escaped += "&gt;"; break;
                case '"':  escaped += "&quot;"; break;
                case '\'': escaped += "&#x27;"; break;
                case '/':  escaped += "&#x2F;"; break;
                default:   escaped += c; break;
            }
        }
        
        return escaped;
    }
    
    // Validate JSON structure (basic check)
    static bool isValidJSON(const std::string& json) {
        int braceCount = 0;
        int bracketCount = 0;
        bool inString = false;
        bool escaped = false;
        
        for (char c : json) {
            if (escaped) {
                escaped = false;
                continue;
            }
            
            if (c == '\\' && inString) {
                escaped = true;
                continue;
            }
            
            if (c == '"' && !inString) {
                inString = true;
            } else if (c == '"' && inString) {
                inString = false;
            }
            
            if (!inString) {
                if (c == '{') braceCount++;
                else if (c == '}') braceCount--;
                else if (c == '[') bracketCount++;
                else if (c == ']') bracketCount--;
                
                if (braceCount < 0 || bracketCount < 0) {
                    return false;
                }
            }
        }
        
        return braceCount == 0 && bracketCount == 0 && !inString;
    }
    
    // Validate numeric range
    static bool validateRange(int value, int min, int max) {
        return value >= min && value <= max;
    }
};

// Example WebSocket message handler with validation
class WebSocketMessageHandler {
private:
    struct ChatMessage {
        std::string username;
        std::string channel;
        std::string content;
        int priority;
    };
    
public:
    bool processMessage(const std::string& rawMessage) {
        // Step 1: Size validation
        if (!InputValidator::validateMessageSize(rawMessage.size())) {
            std::cerr << "Message size exceeds limit\n";
            return false;
        }
        
        // Step 2: UTF-8 validation
        if (!InputValidator::isValidUTF8(rawMessage)) {
            std::cerr << "Invalid UTF-8 encoding\n";
            return false;
        }
        
        // Step 3: JSON structure validation
        if (!InputValidator::isValidJSON(rawMessage)) {
            std::cerr << "Invalid JSON structure\n";
            return false;
        }
        
        // Step 4: Parse and validate fields
        // In production, use a JSON library like nlohmann/json
        ChatMessage msg = parseMessage(rawMessage);
        
        // Step 5: Sanitize and validate individual fields
        msg.username = InputValidator::sanitizeUsername(msg.username);
        if (msg.username.empty()) {
            std::cerr << "Invalid username\n";
            return false;
        }
        
        msg.channel = InputValidator::sanitizeChannel(msg.channel);
        if (msg.channel.empty()) {
            std::cerr << "Invalid channel\n";
            return false;
        }
        
        if (!InputValidator::validateRange(msg.priority, 0, 10)) {
            std::cerr << "Priority out of range\n";
            return false;
        }
        
        // Step 6: Sanitize content for display
        msg.content = InputValidator::htmlEscape(msg.content);
        
        // Message is now safe to process
        std::cout << "Validated message from " << msg.username 
                  << " in " << msg.channel << "\n";
        
        return true;
    }
    
private:
    ChatMessage parseMessage(const std::string& json) {
        // Simplified parsing - use proper JSON library in production
        ChatMessage msg;
        msg.username = "testuser";
        msg.channel = "#general";
        msg.content = "Hello world";
        msg.priority = 5;
        return msg;
    }
};

int main() {
    WebSocketMessageHandler handler;
    
    // Test valid message
    std::string validMsg = R"({"username":"alice","channel":"#general","content":"Hello!","priority":5})";
    handler.processMessage(validMsg);
    
    // Test with XSS attempt
    std::string xssMsg = R"({"username":"alice","channel":"#general","content":"<script>alert('xss')</script>","priority":5})";
    handler.processMessage(xssMsg);
    
    // Test invalid UTF-8
    std::string invalidUtf8 = "\xFF\xFE Invalid UTF-8";
    handler.processMessage(invalidUtf8);
    
    return 0;
}
```

## Rust Implementation

Rust's strong type system and ownership model provide inherent safety advantages for input validation.## Additional Security Considerations

```rust
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::borrow::Cow;

const MAX_MESSAGE_SIZE: usize = 1024 * 1024; // 1MB
const MAX_USERNAME_LENGTH: usize = 50;
const MAX_CHANNEL_LENGTH: usize = 100;
const MAX_CONTENT_LENGTH: usize = 10_000;

#[derive(Debug)]
enum ValidationError {
    MessageTooLarge,
    InvalidUtf8,
    InvalidJson(String),
    InvalidUsername,
    InvalidChannel,
    InvalidPriority,
    ContentTooLong,
    MalformedData,
}

#[derive(Debug, Serialize, Deserialize)]
struct ChatMessage {
    username: String,
    channel: String,
    content: String,
    priority: i32,
}

struct InputValidator;

impl InputValidator {
    /// Validate message size
    fn validate_size(size: usize) -> Result<(), ValidationError> {
        if size == 0 || size > MAX_MESSAGE_SIZE {
            return Err(ValidationError::MessageTooLarge);
        }
        Ok(())
    }
    
    /// Validate UTF-8 encoding (Rust strings are already UTF-8)
    fn validate_utf8(data: &[u8]) -> Result<String, ValidationError> {
        String::from_utf8(data.to_vec())
            .map_err(|_| ValidationError::InvalidUtf8)
    }
    
    /// Sanitize username - alphanumeric and underscores only
    fn sanitize_username(username: &str) -> Result<String, ValidationError> {
        if username.is_empty() || username.len() > MAX_USERNAME_LENGTH {
            return Err(ValidationError::InvalidUsername);
        }
        
        let sanitized: String = username
            .chars()
            .filter(|c| c.is_alphanumeric() || *c == '_')
            .collect();
        
        if sanitized.is_empty() {
            return Err(ValidationError::InvalidUsername);
        }
        
        Ok(sanitized)
    }
    
    /// Validate and sanitize channel name
    fn sanitize_channel(channel: &str) -> Result<String, ValidationError> {
        if channel.is_empty() || channel.len() > MAX_CHANNEL_LENGTH {
            return Err(ValidationError::InvalidChannel);
        }
        
        // Must start with #
        if !channel.starts_with('#') {
            return Err(ValidationError::InvalidChannel);
        }
        
        // Validate format using regex
        let channel_regex = Regex::new(r"^#[a-zA-Z0-9_-]+$").unwrap();
        if !channel_regex.is_match(channel) {
            return Err(ValidationError::InvalidChannel);
        }
        
        Ok(channel.to_string())
    }
    
    /// HTML escape to prevent XSS
    fn html_escape(input: &str) -> String {
        input
            .replace('&', "&amp;")
            .replace('<', "&lt;")
            .replace('>', "&gt;")
            .replace('"', "&quot;")
            .replace('\'', "&#x27;")
            .replace('/', "&#x2F;")
    }
    
    /// Validate priority range
    fn validate_priority(priority: i32) -> Result<i32, ValidationError> {
        if (0..=10).contains(&priority) {
            Ok(priority)
        } else {
            Err(ValidationError::InvalidPriority)
        }
    }
    
    /// Validate content length
    fn validate_content(content: &str) -> Result<(), ValidationError> {
        if content.len() > MAX_CONTENT_LENGTH {
            return Err(ValidationError::ContentTooLong);
        }
        Ok(())
    }
    
    /// SQL escape for database operations (simplified)
    fn sql_escape(input: &str) -> String {
        // In production, use parameterized queries instead
        input.replace('\'', "''")
    }
    
    /// Check for common injection patterns
    fn check_injection_patterns(input: &str) -> Result<(), ValidationError> {
        let dangerous_patterns = [
            "javascript:",
            "onerror=",
            "onload=",
            "<script",
            "</script>",
            "data:text/html",
            "vbscript:",
        ];
        
        let lower = input.to_lowercase();
        for pattern in &dangerous_patterns {
            if lower.contains(pattern) {
                return Err(ValidationError::MalformedData);
            }
        }
        
        Ok(())
    }
}

struct WebSocketMessageHandler;

impl WebSocketMessageHandler {
    /// Process and validate incoming WebSocket message
    fn process_message(&self, raw_data: &[u8]) -> Result<ChatMessage, ValidationError> {
        // Step 1: Size validation
        InputValidator::validate_size(raw_data.len())?;
        
        // Step 2: UTF-8 validation
        let message_str = InputValidator::validate_utf8(raw_data)?;
        
        // Step 3: Parse JSON
        let mut msg: ChatMessage = serde_json::from_str(&message_str)
            .map_err(|e| ValidationError::InvalidJson(e.to_string()))?;
        
        // Step 4: Validate and sanitize username
        msg.username = InputValidator::sanitize_username(&msg.username)?;
        
        // Step 5: Validate and sanitize channel
        msg.channel = InputValidator::sanitize_channel(&msg.channel)?;
        
        // Step 6: Validate priority
        msg.priority = InputValidator::validate_priority(msg.priority)?;
        
        // Step 7: Validate content length
        InputValidator::validate_content(&msg.content)?;
        
        // Step 8: Check for injection patterns
        InputValidator::check_injection_patterns(&msg.content)?;
        
        // Step 9: HTML escape content for safe display
        msg.content = InputValidator::html_escape(&msg.content);
        
        println!("✓ Validated message from {} in {}", msg.username, msg.channel);
        
        Ok(msg)
    }
}

// Advanced validator with custom rules
struct CustomValidator {
    allowed_channels: Vec<String>,
    max_message_rate: u32,
}

impl CustomValidator {
    fn new(allowed_channels: Vec<String>) -> Self {
        Self {
            allowed_channels,
            max_message_rate: 10,
        }
    }
    
    /// Validate channel is in whitelist
    fn validate_channel_whitelist(&self, channel: &str) -> Result<(), ValidationError> {
        if self.allowed_channels.contains(&channel.to_string()) {
            Ok(())
        } else {
            Err(ValidationError::InvalidChannel)
        }
    }
    
    /// Validate message against business logic
    fn validate_business_rules(&self, msg: &ChatMessage) -> Result<(), ValidationError> {
        // Example: High priority messages must have certain prefix
        if msg.priority >= 8 && !msg.content.starts_with("[URGENT]") {
            return Err(ValidationError::MalformedData);
        }
        
        Ok(())
    }
}

fn main() {
    let handler = WebSocketMessageHandler;
    
    // Test 1: Valid message
    println!("\n=== Test 1: Valid Message ===");
    let valid_msg = r#"{"username":"alice","channel":"#general","content":"Hello everyone!","priority":5}"#;
    match handler.process_message(valid_msg.as_bytes()) {
        Ok(msg) => println!("Success: {:?}", msg),
        Err(e) => println!("Error: {:?}", e),
    }
    
    // Test 2: XSS attempt
    println!("\n=== Test 2: XSS Attempt ===");
    let xss_msg = r#"{"username":"alice","channel":"#general","content":"<script>alert('xss')</script>","priority":5}"#;
    match handler.process_message(xss_msg.as_bytes()) {
        Ok(msg) => println!("Success (sanitized): {:?}", msg),
        Err(e) => println!("Error: {:?}", e),
    }
    
    // Test 3: Invalid username
    println!("\n=== Test 3: Invalid Username ===");
    let invalid_user = r#"{"username":"user@#$%","channel":"#general","content":"Hello","priority":5}"#;
    match handler.process_message(invalid_user.as_bytes()) {
        Ok(msg) => println!("Success: {:?}", msg),
        Err(e) => println!("Error: {:?}", e),
    }
    
    // Test 4: Invalid channel
    println!("\n=== Test 4: Invalid Channel ===");
    let invalid_channel = r#"{"username":"alice","channel":"general","content":"Hello","priority":5}"#;
    match handler.process_message(invalid_channel.as_bytes()) {
        Ok(msg) => println!("Success: {:?}", msg),
        Err(e) => println!("Error: {:?}", e),
    }
    
    // Test 5: Priority out of range
    println!("\n=== Test 5: Invalid Priority ===");
    let invalid_priority = r#"{"username":"alice","channel":"#general","content":"Hello","priority":99}"#;
    match handler.process_message(invalid_priority.as_bytes()) {
        Ok(msg) => println!("Success: {:?}", msg),
        Err(e) => println!("Error: {:?}", e),
    }
    
    // Test 6: Custom validator with whitelist
    println!("\n=== Test 6: Channel Whitelist ===");
    let custom_validator = CustomValidator::new(vec![
        "#general".to_string(),
        "#announcements".to_string(),
    ]);
    
    match custom_validator.validate_channel_whitelist("#general") {
        Ok(_) => println!("✓ Channel allowed"),
        Err(e) => println!("✗ Channel rejected: {:?}", e),
    }
    
    match custom_validator.validate_channel_whitelist("#random") {
        Ok(_) => println!("✓ Channel allowed"),
        Err(e) => println!("✗ Channel rejected: {:?}", e),
    }
}
```

### 1. **Origin Validation During Handshake**

Always validate the `Origin` header during the WebSocket handshake to prevent Cross-Site WebSocket Hijacking:

```cpp
// C++ example
bool validateOrigin(const std::string& origin) {
    const std::vector<std::string> allowedOrigins = {
        "https://example.com",
        "https://app.example.com"
    };
    
    return std::find(allowedOrigins.begin(), 
                    allowedOrigins.end(), 
                    origin) != allowedOrigins.end();
}
```

```rust
// Rust example
fn validate_origin(origin: &str) -> bool {
    const ALLOWED_ORIGINS: &[&str] = &[
        "https://example.com",
        "https://app.example.com",
    ];
    
    ALLOWED_ORIGINS.contains(&origin)
}
```

### 2. **Rate Limiting**

Implement rate limiting to prevent flooding attacks:

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};

struct RateLimiter {
    requests: HashMap<String, Vec<Instant>>,
    max_requests: usize,
    window: Duration,
}

impl RateLimiter {
    fn new(max_requests: usize, window_secs: u64) -> Self {
        Self {
            requests: HashMap::new(),
            max_requests,
            window: Duration::from_secs(window_secs),
        }
    }
    
    fn check_rate(&mut self, client_id: &str) -> bool {
        let now = Instant::now();
        let entry = self.requests.entry(client_id.to_string())
            .or_insert_with(Vec::new);
        
        // Remove old entries
        entry.retain(|&time| now.duration_since(time) < self.window);
        
        if entry.len() >= self.max_requests {
            return false;
        }
        
        entry.push(now);
        true
    }
}
```

### 3. **Message Size and Frame Limits**

Protect against memory exhaustion by enforcing strict limits on individual frames and complete messages:

```cpp
constexpr size_t MAX_FRAME_SIZE = 64 * 1024;  // 64KB per frame
constexpr size_t MAX_TOTAL_MESSAGE = 1024 * 1024;  // 1MB total
constexpr size_t MAX_FRAGMENTS = 100;  // Max fragmented frames
```

### 4. **Binary Data Validation**

For binary WebSocket messages, validate magic numbers, checksums, and protocol-specific headers:

```rust
fn validate_binary_message(data: &[u8]) -> Result<(), ValidationError> {
    if data.len() < 4 {
        return Err(ValidationError::MalformedData);
    }
    
    // Check magic number
    let magic = u32::from_be_bytes([data[0], data[1], data[2], data[3]]);
    if magic != 0xDEADBEEF {
        return Err(ValidationError::MalformedData);
    }
    
    // Validate checksum, length fields, etc.
    Ok(())
}
```

## Summary

Input validation and sanitization form the foundation of WebSocket security. Key takeaways include:

**Critical Defenses:**
- **Size limits**: Prevent resource exhaustion with strict message and frame size caps
- **UTF-8 validation**: Ensure text data is properly encoded (especially important in C/C++)
- **Format validation**: Verify JSON structure, protocol compliance, and data schemas
- **Content sanitization**: HTML escape output, SQL parameterization for queries, and path normalization
- **Injection prevention**: Screen for dangerous patterns like script tags, SQL keywords, and command sequences

**Language-Specific Considerations:**
- **C/C++**: Requires manual bounds checking, buffer management, and careful string handling. Use modern C++ features (std::string, smart pointers) and established libraries
- **Rust**: Provides memory safety guarantees and UTF-8 validation at compile time. The type system enforces validation through Result types and pattern matching

**Best Practices:**
- Validate at multiple layers: handshake, protocol, application, and business logic
- Implement rate limiting and throttling per connection
- Use whitelisting over blacklisting for allowed values
- Sanitize differently for different contexts (HTML display vs database storage vs command execution)
- Log suspicious activity for security monitoring
- Never trust client input, even from authenticated sources
- Use established libraries for complex validation (regex, JSON parsing, protocol handling)

WebSocket security requires ongoing vigilance since connections are long-lived and bidirectional. A single validation oversight can expose the entire application to persistent attacks. By implementing comprehensive validation in both C/C++ and Rust, developers can build robust, secure real-time applications.