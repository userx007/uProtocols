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
