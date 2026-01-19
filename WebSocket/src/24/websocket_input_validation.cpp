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