# Access Control Lists in MQTT

## Overview

Access Control Lists (ACLs) in MQTT provide fine-grained authorization mechanisms that control which clients can publish to or subscribe to specific topics. While authentication verifies *who* a client is, ACLs determine *what* that client is allowed to do. This topic-level authorization is crucial for securing MQTT deployments in production environments.

## Core Concepts

**Authorization vs Authentication**: Authentication establishes identity (username/password, certificates), while authorization determines permissions (what topics can be accessed). ACLs handle the authorization layer.

**Permission Types**: MQTT ACLs typically define three types of permissions:
- **Read (Subscribe)**: Ability to subscribe to topics and receive messages
- **Write (Publish)**: Ability to publish messages to topics  
- **Read/Write**: Combined permissions for both operations

**Topic Patterns**: ACLs support wildcard patterns matching MQTT's topic structure:
- Single-level wildcard (`+`) matches one topic level
- Multi-level wildcard (`#`) matches multiple levels at the end of a topic
- Exact matches for specific topics

**Rule Evaluation**: Most brokers evaluate ACL rules in order, with the first matching rule determining access. Rules can be allow or deny, with common default-deny policies for security.

## Implementation Architecture

ACLs can be implemented through various mechanisms:

**File-based ACLs**: Rules stored in configuration files, simple but requiring broker restart for updates.

**Database-backed ACLs**: Dynamic rules stored in databases (PostgreSQL, MySQL, MongoDB), allowing runtime updates without restarts.

**Plugin-based ACLs**: Custom authorization logic through broker plugins, enabling integration with existing identity systems.

**External authorization services**: RESTful APIs or gRPC services that brokers query for access decisions, supporting complex enterprise authorization policies.

## C/C++ Implementation Examples

### Using Mosquitto Broker with ACL Configuration

```c
// mosquitto_acl_check.c
// Example of programmatically checking ACLs in Mosquitto plugin
#include <stdio.h>
#include <string.h>
#include <mosquitto.h>
#include <mosquitto_broker.h>
#include <mosquitto_plugin.h>

// ACL check callback function
int mosquitto_auth_acl_check(void *user_data, int access, 
                             struct mosquitto *client,
                             const struct mosquitto_acl_msg *msg)
{
    const char *username = mosquitto_client_username(client);
    const char *topic = msg->topic;
    
    // Example: Admin user has full access
    if (username && strcmp(username, "admin") == 0) {
        return MOSQ_ERR_SUCCESS;
    }
    
    // Example: Sensor clients can only publish to sensors/* topics
    if (username && strncmp(username, "sensor_", 7) == 0) {
        if (access == MOSQ_ACL_WRITE && 
            strncmp(topic, "sensors/", 8) == 0) {
            return MOSQ_ERR_SUCCESS;
        }
    }
    
    // Example: Monitor clients can only subscribe to all topics
    if (username && strcmp(username, "monitor") == 0) {
        if (access == MOSQ_ACL_READ) {
            return MOSQ_ERR_SUCCESS;
        }
    }
    
    // Deny by default
    return MOSQ_ERR_ACL_DENIED;
}

// Plugin initialization
int mosquitto_auth_plugin_init(void **user_data, 
                               struct mosquitto_opt *opts, int opt_count)
{
    printf("ACL plugin initialized\n");
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_plugin_cleanup(void *user_data, 
                                  struct mosquitto_opt *opts, int opt_count)
{
    printf("ACL plugin cleanup\n");
    return MOSQ_ERR_SUCCESS;
}
```

### Client-Side ACL Handling in C

```c
// mqtt_client_acl.c
// MQTT client with ACL error handling
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>

struct client_data {
    char *username;
    int connection_status;
};

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    struct client_data *data = (struct client_data *)obj;
    
    if (rc == 0) {
        printf("Connected successfully as %s\n", data->username);
        data->connection_status = 1;
        
        // Attempt to subscribe to a topic
        int mid;
        int result = mosquitto_subscribe(mosq, &mid, "sensors/#", 0);
        
        if (result != MOSQ_ERR_SUCCESS) {
            printf("Subscribe failed: %s\n", mosquitto_strerror(result));
        }
    } else {
        printf("Connection failed: %s\n", mosquitto_connack_string(rc));
        data->connection_status = 0;
    }
}

void on_subscribe(struct mosquitto *mosq, void *obj, 
                  int mid, int qos_count, const int *granted_qos) {
    printf("Subscription granted with QoS: %d\n", granted_qos[0]);
    
    // QoS 128 (0x80) indicates subscription failure due to ACL
    if (granted_qos[0] == 128) {
        printf("ERROR: Subscription denied by ACL\n");
    }
}

void on_publish(struct mosquitto *mosq, void *obj, int mid) {
    printf("Message published successfully (mid: %d)\n", mid);
}

void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    printf("Received: %s = %s\n", msg->topic, (char *)msg->payload);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <host> <username> <password>\n", argv[0]);
        return 1;
    }
    
    struct client_data data = {
        .username = argv[2],
        .connection_status = 0
    };
    
    mosquitto_lib_init();
    
    struct mosquitto *mosq = mosquitto_new(NULL, true, &data);
    if (!mosq) {
        fprintf(stderr, "Error creating mosquitto instance\n");
        return 1;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_subscribe_callback_set(mosq, on_subscribe);
    mosquitto_publish_callback_set(mosq, on_publish);
    mosquitto_message_callback_set(mosq, on_message);
    
    // Set username and password
    mosquitto_username_pw_set(mosq, argv[2], argv[3]);
    
    // Connect to broker
    if (mosquitto_connect(mosq, argv[1], 1883, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Unable to connect\n");
        return 1;
    }
    
    // Try to publish to a topic (may be denied by ACL)
    int rc = mosquitto_publish(mosq, NULL, "sensors/temperature", 
                               5, "25.5", 0, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("Publish failed: %s\n", mosquitto_strerror(rc));
    }
    
    // Run event loop
    mosquitto_loop_forever(mosq, -1, 1);
    
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

### ACL Configuration File Parser in C++

```cpp
// acl_parser.cpp
// Parse and validate ACL configuration files
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>

enum class Permission {
    READ,
    WRITE,
    READWRITE,
    DENY
};

struct ACLRule {
    std::string username;
    Permission permission;
    std::string topic_pattern;
};

class ACLManager {
private:
    std::vector<ACLRule> rules;
    
public:
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open ACL file: " << filename << std::endl;
            return false;
        }
        
        std::string line;
        std::string current_user;
        
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') continue;
            
            // User declaration: user <username>
            if (line.substr(0, 5) == "user ") {
                current_user = line.substr(5);
                continue;
            }
            
            // Topic rule: topic [read|write|readwrite|deny] <pattern>
            if (line.substr(0, 6) == "topic ") {
                if (current_user.empty()) {
                    std::cerr << "Topic rule without user context" << std::endl;
                    continue;
                }
                
                ACLRule rule;
                rule.username = current_user;
                
                size_t pos = 6; // After "topic "
                size_t space_pos = line.find(' ', pos);
                
                std::string perm_str = line.substr(pos, space_pos - pos);
                std::string topic = line.substr(space_pos + 1);
                
                if (perm_str == "read") rule.permission = Permission::READ;
                else if (perm_str == "write") rule.permission = Permission::WRITE;
                else if (perm_str == "readwrite") rule.permission = Permission::READWRITE;
                else if (perm_str == "deny") rule.permission = Permission::DENY;
                else continue;
                
                rule.topic_pattern = topic;
                rules.push_back(rule);
            }
        }
        
        file.close();
        return true;
    }
    
    bool checkAccess(const std::string& username, 
                    const std::string& topic,
                    bool is_write) const {
        for (const auto& rule : rules) {
            if (rule.username != username) continue;
            
            if (matchTopic(rule.topic_pattern, topic)) {
                switch (rule.permission) {
                    case Permission::DENY:
                        return false;
                    case Permission::READ:
                        return !is_write;
                    case Permission::WRITE:
                        return is_write;
                    case Permission::READWRITE:
                        return true;
                }
            }
        }
        
        // Default deny
        return false;
    }
    
    void printRules() const {
        std::cout << "ACL Rules loaded:\n";
        for (const auto& rule : rules) {
            std::cout << "User: " << rule.username 
                      << ", Permission: " << static_cast<int>(rule.permission)
                      << ", Topic: " << rule.topic_pattern << std::endl;
        }
    }

private:
    bool matchTopic(const std::string& pattern, const std::string& topic) const {
        // Convert MQTT wildcards to regex
        std::string regex_pattern = pattern;
        
        // Escape special regex characters
        regex_pattern = std::regex_replace(regex_pattern, std::regex("\\."), "\\.");
        
        // Replace MQTT wildcards
        regex_pattern = std::regex_replace(regex_pattern, std::regex("\\+"), "[^/]+");
        regex_pattern = std::regex_replace(regex_pattern, std::regex("#"), ".*");
        
        // Anchor the pattern
        regex_pattern = "^" + regex_pattern + "$";
        
        std::regex re(regex_pattern);
        return std::regex_match(topic, re);
    }
};

int main() {
    ACLManager acl;
    
    // Load ACL configuration
    if (!acl.loadFromFile("acl.conf")) {
        return 1;
    }
    
    acl.printRules();
    
    // Test access control
    std::cout << "\nAccess Control Tests:\n";
    
    // Test cases
    struct TestCase {
        std::string username;
        std::string topic;
        bool is_write;
        bool expected;
    };
    
    std::vector<TestCase> tests = {
        {"sensor_01", "sensors/temperature", true, true},
        {"sensor_01", "actuators/valve", true, false},
        {"monitor", "sensors/temperature", false, true},
        {"monitor", "sensors/temperature", true, false},
        {"admin", "system/config", true, true}
    };
    
    for (const auto& test : tests) {
        bool result = acl.checkAccess(test.username, test.topic, test.is_write);
        std::cout << "User: " << test.username 
                  << ", Topic: " << test.topic
                  << ", Write: " << test.is_write
                  << " => " << (result ? "ALLOWED" : "DENIED")
                  << " (expected: " << (test.expected ? "ALLOWED" : "DENIED") << ")"
                  << std::endl;
    }
    
    return 0;
}
```

## Rust Implementation Examples

### MQTT Client with ACL Error Handling

```rust
// mqtt_acl_client.rs
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::Duration;
use std::error::Error;

#[derive(Debug)]
struct MqttAclClient {
    username: String,
    client: AsyncClient,
}

impl MqttAclClient {
    async fn new(
        broker: &str,
        port: u16,
        username: &str,
        password: &str,
    ) -> Result<Self, Box<dyn Error>> {
        let mut mqttoptions = MqttOptions::new(
            format!("acl_client_{}", username),
            broker,
            port
        );
        
        mqttoptions
            .set_credentials(username, password)
            .set_keep_alive(Duration::from_secs(30));
        
        let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
        
        // Spawn event handler
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(notification) => {
                        if let Event::Incoming(Packet::SubAck(suback)) = notification {
                            for code in &suback.return_codes {
                                // 0x80 indicates subscription failure (ACL denied)
                                if *code == 0x80 {
                                    eprintln!("Subscription denied by ACL!");
                                }
                            }
                        }
                    }
                    Err(e) => {
                        eprintln!("Connection error: {:?}", e);
                        tokio::time::sleep(Duration::from_secs(5)).await;
                    }
                }
            }
        });
        
        Ok(Self {
            username: username.to_string(),
            client,
        })
    }
    
    async fn publish(&self, topic: &str, payload: &str) -> Result<(), Box<dyn Error>> {
        match self.client.publish(topic, QoS::AtLeastOnce, false, payload).await {
            Ok(_) => {
                println!("Published to {}: {}", topic, payload);
                Ok(())
            }
            Err(e) => {
                eprintln!("Publish failed (possible ACL denial): {:?}", e);
                Err(Box::new(e))
            }
        }
    }
    
    async fn subscribe(&self, topic: &str) -> Result<(), Box<dyn Error>> {
        match self.client.subscribe(topic, QoS::AtLeastOnce).await {
            Ok(_) => {
                println!("Subscribed to {}", topic);
                Ok(())
            }
            Err(e) => {
                eprintln!("Subscribe failed (possible ACL denial): {:?}", e);
                Err(Box::new(e))
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Example: Sensor client with limited permissions
    let sensor_client = MqttAclClient::new(
        "localhost",
        1883,
        "sensor_01",
        "sensor_password"
    ).await?;
    
    // This should succeed (sensor can publish to sensors/*)
    sensor_client.publish("sensors/temperature", "22.5").await?;
    
    // This might fail if ACL denies (sensor cannot publish to actuators/*)
    let _ = sensor_client.publish("actuators/valve", "open").await;
    
    // Monitor client with read-only permissions
    let monitor_client = MqttAclClient::new(
        "localhost",
        1883,
        "monitor",
        "monitor_password"
    ).await?;
    
    // This should succeed (monitor can subscribe)
    monitor_client.subscribe("sensors/#").await?;
    
    // This might fail if ACL denies (monitor cannot publish)
    let _ = monitor_client.publish("sensors/humidity", "60").await;
    
    tokio::time::sleep(Duration::from_secs(60)).await;
    Ok(())
}
```

### ACL Rule Engine in Rust

```rust
// acl_engine.rs
use std::collections::HashMap;
use regex::Regex;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
enum Permission {
    Read,
    Write,
    ReadWrite,
    Deny,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct AclRule {
    username: String,
    permission: Permission,
    topic_pattern: String,
}

#[derive(Debug)]
struct AclEngine {
    rules: Vec<AclRule>,
    compiled_patterns: HashMap<String, Regex>,
}

impl AclEngine {
    fn new() -> Self {
        Self {
            rules: Vec::new(),
            compiled_patterns: HashMap::new(),
        }
    }
    
    fn add_rule(&mut self, rule: AclRule) {
        // Compile MQTT topic pattern to regex
        let regex_pattern = Self::mqtt_pattern_to_regex(&rule.topic_pattern);
        self.compiled_patterns.insert(
            rule.topic_pattern.clone(),
            Regex::new(&regex_pattern).unwrap()
        );
        self.rules.push(rule);
    }
    
    fn load_from_config(&mut self, config: &str) -> Result<(), String> {
        let mut current_user = String::new();
        
        for line in config.lines() {
            let line = line.trim();
            
            // Skip comments and empty lines
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            
            // Parse user declaration
            if let Some(username) = line.strip_prefix("user ") {
                current_user = username.trim().to_string();
                continue;
            }
            
            // Parse topic rule
            if let Some(rule_str) = line.strip_prefix("topic ") {
                if current_user.is_empty() {
                    return Err("Topic rule without user context".to_string());
                }
                
                let parts: Vec<&str> = rule_str.splitn(2, ' ').collect();
                if parts.len() != 2 {
                    return Err(format!("Invalid topic rule: {}", line));
                }
                
                let permission = match parts[0] {
                    "read" => Permission::Read,
                    "write" => Permission::Write,
                    "readwrite" => Permission::ReadWrite,
                    "deny" => Permission::Deny,
                    _ => return Err(format!("Invalid permission: {}", parts[0])),
                };
                
                self.add_rule(AclRule {
                    username: current_user.clone(),
                    permission,
                    topic_pattern: parts[1].to_string(),
                });
            }
        }
        
        Ok(())
    }
    
    fn check_access(&self, username: &str, topic: &str, is_write: bool) -> bool {
        for rule in &self.rules {
            if rule.username != username {
                continue;
            }
            
            // Check if topic matches pattern
            if let Some(regex) = self.compiled_patterns.get(&rule.topic_pattern) {
                if regex.is_match(topic) {
                    return match rule.permission {
                        Permission::Deny => false,
                        Permission::Read => !is_write,
                        Permission::Write => is_write,
                        Permission::ReadWrite => true,
                    };
                }
            }
        }
        
        // Default deny
        false
    }
    
    fn mqtt_pattern_to_regex(pattern: &str) -> String {
        let mut regex = String::from("^");
        
        for ch in pattern.chars() {
            match ch {
                '+' => regex.push_str("[^/]+"),
                '#' => regex.push_str(".*"),
                '/' => regex.push('/'),
                '.' => regex.push_str("\\."),
                _ => regex.push(ch),
            }
        }
        
        regex.push('$');
        regex
    }
    
    fn get_user_permissions(&self, username: &str) -> Vec<&AclRule> {
        self.rules.iter()
            .filter(|rule| rule.username == username)
            .collect()
    }
}

fn main() {
    let mut acl = AclEngine::new();
    
    // Load ACL configuration
    let config = r#"
        # Admin user - full access
        user admin
        topic readwrite #
        
        # Sensor devices - can only publish to sensors/*
        user sensor_01
        topic write sensors/+
        topic deny #
        
        # Monitor application - read-only access
        user monitor
        topic read #
        topic deny actuators/#
        
        # Actuator devices - can publish and subscribe to actuators/*
        user actuator_valve
        topic readwrite actuators/valve/#
        topic read sensors/#
    "#;
    
    match acl.load_from_config(config) {
        Ok(_) => println!("ACL configuration loaded successfully"),
        Err(e) => eprintln!("Error loading ACL: {}", e),
    }
    
    // Test cases
    println!("\n=== Access Control Tests ===\n");
    
    let tests = vec![
        ("admin", "system/config", true, "Admin publish to system/config"),
        ("sensor_01", "sensors/temperature", true, "Sensor publish to sensors/temperature"),
        ("sensor_01", "actuators/valve", true, "Sensor publish to actuators/valve"),
        ("monitor", "sensors/humidity", false, "Monitor subscribe to sensors/humidity"),
        ("monitor", "sensors/humidity", true, "Monitor publish to sensors/humidity"),
        ("actuator_valve", "actuators/valve/state", true, "Actuator publish to own topic"),
        ("actuator_valve", "actuators/valve/state", false, "Actuator subscribe to own topic"),
        ("actuator_valve", "sensors/temperature", false, "Actuator subscribe to sensors"),
    ];
    
    for (username, topic, is_write, description) in tests {
        let allowed = acl.check_access(username, topic, is_write);
        let action = if is_write { "PUBLISH" } else { "SUBSCRIBE" };
        let result = if allowed { "✓ ALLOWED" } else { "✗ DENIED" };
        
        println!("[{}] {} - {} to {}", result, description, action, topic);
    }
    
    // Display user permissions
    println!("\n=== User Permissions Summary ===\n");
    for username in ["admin", "sensor_01", "monitor", "actuator_valve"] {
        println!("User: {}", username);
        for rule in acl.get_user_permissions(username) {
            println!("  {:?} {}", rule.permission, rule.topic_pattern);
        }
        println!();
    }
}
```

### Dynamic ACL with Database Backend (Concept)

```rust
// dynamic_acl.rs
use sqlx::{Pool, Postgres, FromRow};
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Debug, Clone, FromRow)]
struct DbAclRule {
    id: i32,
    username: String,
    permission: String,
    topic_pattern: String,
    enabled: bool,
}

struct DynamicAclEngine {
    db_pool: Pool<Postgres>,
    cache: Arc<RwLock<Vec<DbAclRule>>>,
}

impl DynamicAclEngine {
    async fn new(database_url: &str) -> Result<Self, sqlx::Error> {
        let pool = sqlx::postgres::PgPoolOptions::new()
            .max_connections(5)
            .connect(database_url)
            .await?;
        
        Ok(Self {
            db_pool: pool,
            cache: Arc::new(RwLock::new(Vec::new())),
        })
    }
    
    async fn reload_rules(&self) -> Result<(), sqlx::Error> {
        let rules = sqlx::query_as::<_, DbAclRule>(
            "SELECT id, username, permission, topic_pattern, enabled 
             FROM acl_rules WHERE enabled = true ORDER BY id"
        )
        .fetch_all(&self.db_pool)
        .await?;
        
        let mut cache = self.cache.write().await;
        *cache = rules;
        
        Ok(())
    }
    
    async fn check_access(&self, username: &str, topic: &str, is_write: bool) -> bool {
        let rules = self.cache.read().await;
        
        for rule in rules.iter() {
            if rule.username != username {
                continue;
            }
            
            if self.matches_pattern(&rule.topic_pattern, topic) {
                return match rule.permission.as_str() {
                    "deny" => false,
                    "read" => !is_write,
                    "write" => is_write,
                    "readwrite" => true,
                    _ => false,
                };
            }
        }
        
        false
    }
    
    async fn add_rule(
        &self,
        username: &str,
        permission: &str,
        topic_pattern: &str,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO acl_rules (username, permission, topic_pattern, enabled) 
             VALUES ($1, $2, $3, true)"
        )
        .bind(username)
        .bind(permission)
        .bind(topic_pattern)
        .execute(&self.db_pool)
        .await?;
        
        self.reload_rules().await?;
        Ok(())
    }
    
    fn matches_pattern(&self, pattern: &str, topic: &str) -> bool {
        let pattern_parts: Vec<&str> = pattern.split('/').collect();
        let topic_parts: Vec<&str> = topic.split('/').collect();
        
        let mut i = 0;
        let mut j = 0;
        
        while i < pattern_parts.len() && j < topic_parts.len() {
            if pattern_parts[i] == "#" {
                return true;
            }
            if pattern_parts[i] == "+" || pattern_parts[i] == topic_parts[j] {
                i += 1;
                j += 1;
            } else {
                return false;
            }
        }
        
        i == pattern_parts.len() && j == topic_parts.len()
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // This is a conceptual example - requires actual PostgreSQL database
    println!("Dynamic ACL Engine Example");
    println!("This demonstrates database-backed ACL management");
    
    // In production, would connect to actual database:
    // let acl = DynamicAclEngine::new("postgresql://user:pass@localhost/mqtt_acl").await?;
    // acl.reload_rules().await?;
    // let allowed = acl.check_access("sensor_01", "sensors/temp", true).await;
    
    Ok(())
}
```

## Summary

Access Control Lists are a critical security mechanism in MQTT that provide fine-grained authorization at the topic level. They complement authentication by defining what authenticated clients can do, specifically controlling publish and subscribe permissions across topic hierarchies.

Key implementation considerations include choosing between file-based, database-backed, or plugin-based approaches depending on your deployment's scale and requirements. File-based ACLs are simple but static, while database-backed solutions offer runtime flexibility essential for large-scale deployments. The examples demonstrate both client-side handling of ACL denials and server-side rule enforcement.

Modern MQTT deployments typically combine ACLs with other security measures including TLS encryption, certificate-based authentication, and audit logging. The rule evaluation process follows a first-match policy with pattern matching supporting MQTT's wildcard operators, enabling flexible yet secure topic-level access control. Proper ACL design requires careful planning of topic hierarchies, user roles, and permission granularity to balance security with operational flexibility.