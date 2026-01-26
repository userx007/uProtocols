# Intrusion Detection in MQTT

## Detailed Description

Intrusion Detection in MQTT systems involves monitoring message traffic, connection patterns, and protocol behavior to identify potential security threats, anomalies, and malicious activities. This is critical for protecting IoT deployments from unauthorized access, data breaches, denial-of-service attacks, and other cyber threats.

### Key Components

**1. Traffic Analysis**
- Message rate monitoring to detect flooding attacks
- Payload inspection for malicious content or suspicious patterns
- Connection frequency analysis to identify brute-force attempts
- Topic subscription patterns that deviate from normal behavior

**2. Anomaly Detection**
- Baseline establishment of normal MQTT traffic patterns
- Statistical deviation detection using thresholds
- Machine learning-based behavioral analysis
- Time-series analysis for temporal anomalies

**3. Threat Detection**
- Unauthorized topic access attempts
- Invalid authentication attempts
- Protocol violations and malformed packets
- Suspicious payload content (SQL injection, command injection)
- Client impersonation attempts

**4. Response Mechanisms**
- Real-time alerting to security teams
- Automatic client disconnection or blocking
- Rate limiting enforcement
- Logging and forensic data collection

## C/C++ Implementation

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CLIENTS 1000
#define ALERT_THRESHOLD 100  // Messages per second
#define TIME_WINDOW 1        // seconds

typedef struct {
    char client_id[64];
    int message_count;
    time_t last_reset;
    int failed_auth_attempts;
    time_t last_seen;
} ClientStats;

typedef struct {
    ClientStats clients[MAX_CLIENTS];
    int client_count;
    FILE *alert_log;
} IDSContext;

// Initialize IDS context
IDSContext* ids_init() {
    IDSContext *ctx = (IDSContext*)calloc(1, sizeof(IDSContext));
    ctx->alert_log = fopen("/var/log/mqtt_ids.log", "a");
    return ctx;
}

// Log security alert
void log_alert(IDSContext *ctx, const char *alert_type, 
               const char *client_id, const char *details) {
    time_t now = time(NULL);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0';  // Remove newline
    
    fprintf(ctx->alert_log, "[%s] ALERT: %s - Client: %s - %s\n",
            timestamp, alert_type, client_id, details);
    fflush(ctx->alert_log);
    
    printf("🚨 SECURITY ALERT: %s from %s\n", alert_type, client_id);
}

// Find or create client statistics
ClientStats* get_client_stats(IDSContext *ctx, const char *client_id) {
    // Search for existing client
    for (int i = 0; i < ctx->client_count; i++) {
        if (strcmp(ctx->clients[i].client_id, client_id) == 0) {
            return &ctx->clients[i];
        }
    }
    
    // Create new client entry
    if (ctx->client_count < MAX_CLIENTS) {
        ClientStats *stats = &ctx->clients[ctx->client_count++];
        strncpy(stats->client_id, client_id, sizeof(stats->client_id) - 1);
        stats->last_reset = time(NULL);
        return stats;
    }
    
    return NULL;
}

// Check for rate-based anomalies
int check_rate_anomaly(IDSContext *ctx, const char *client_id) {
    ClientStats *stats = get_client_stats(ctx, client_id);
    if (!stats) return 0;
    
    time_t now = time(NULL);
    
    // Reset counter if time window elapsed
    if (now - stats->last_reset >= TIME_WINDOW) {
        stats->message_count = 0;
        stats->last_reset = now;
    }
    
    stats->message_count++;
    stats->last_seen = now;
    
    // Check threshold
    if (stats->message_count > ALERT_THRESHOLD) {
        char details[128];
        snprintf(details, sizeof(details), 
                "Rate limit exceeded: %d msgs/%d sec", 
                stats->message_count, TIME_WINDOW);
        log_alert(ctx, "RATE_ANOMALY", client_id, details);
        return 1;
    }
    
    return 0;
}

// Inspect payload for suspicious patterns
int check_payload_threat(IDSContext *ctx, const char *client_id, 
                         const char *payload, int len) {
    const char *sql_patterns[] = {"SELECT ", "DROP ", "DELETE ", 
                                  "'; --", "UNION ", NULL};
    const char *cmd_patterns[] = {"; rm -rf", "$(", "`", 
                                  "&& rm", "| bash", NULL};
    
    // Check for SQL injection patterns
    for (int i = 0; sql_patterns[i] != NULL; i++) {
        if (strcasestr(payload, sql_patterns[i])) {
            log_alert(ctx, "SQL_INJECTION", client_id, 
                     "Potential SQL injection detected in payload");
            return 1;
        }
    }
    
    // Check for command injection patterns
    for (int i = 0; cmd_patterns[i] != NULL; i++) {
        if (strstr(payload, cmd_patterns[i])) {
            log_alert(ctx, "CMD_INJECTION", client_id, 
                     "Potential command injection detected");
            return 1;
        }
    }
    
    return 0;
}

// Message callback with IDS inspection
void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    IDSContext *ctx = (IDSContext*)obj;
    const char *client_id = mosquitto_property_read_string(
        msg->properties, MQTT_PROP_USER_PROPERTY, NULL, NULL);
    
    if (!client_id) client_id = "unknown";
    
    // Check rate anomalies
    if (check_rate_anomaly(ctx, client_id)) {
        // Could trigger rate limiting here
    }
    
    // Check payload threats
    if (msg->payload && msg->payloadlen > 0) {
        check_payload_threat(ctx, client_id, 
                           (const char*)msg->payload, msg->payloadlen);
    }
    
    // Check for suspicious topics
    if (strstr(msg->topic, "admin") || strstr(msg->topic, "config")) {
        char details[256];
        snprintf(details, sizeof(details), 
                "Access to sensitive topic: %s", msg->topic);
        log_alert(ctx, "SENSITIVE_TOPIC", client_id, details);
    }
}

int main() {
    mosquitto_lib_init();
    
    IDSContext *ids_ctx = ids_init();
    struct mosquitto *mosq = mosquitto_new("mqtt_ids_monitor", true, ids_ctx);
    
    if (!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }
    
    mosquitto_message_callback_set(mosq, on_message);
    
    // Connect to broker
    if (mosquitto_connect(mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to connect to broker\n");
        return 1;
    }
    
    // Subscribe to all topics for monitoring
    mosquitto_subscribe(mosq, NULL, "#", 0);
    
    printf("MQTT IDS Monitor started...\n");
    mosquitto_loop_forever(mosq, -1, 1);
    
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    fclose(ids_ctx->alert_log);
    free(ids_ctx);
    
    return 0;
}
```

## Rust Implementation

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tokio::time;
use regex::Regex;

#[derive(Clone)]
struct ClientStats {
    client_id: String,
    message_count: u32,
    last_reset: u64,
    failed_auth_attempts: u32,
    last_seen: u64,
}

struct IntrusionDetector {
    clients: Arc<Mutex<HashMap<String, ClientStats>>>,
    alert_threshold: u32,
    time_window: u64,
    sql_patterns: Vec<Regex>,
    cmd_patterns: Vec<Regex>,
}

impl IntrusionDetector {
    fn new(alert_threshold: u32, time_window: u64) -> Self {
        let sql_patterns = vec![
            Regex::new(r"(?i)SELECT\s+").unwrap(),
            Regex::new(r"(?i)DROP\s+").unwrap(),
            Regex::new(r"(?i)DELETE\s+").unwrap(),
            Regex::new(r"';\s*--").unwrap(),
            Regex::new(r"(?i)UNION\s+").unwrap(),
        ];
        
        let cmd_patterns = vec![
            Regex::new(r";\s*rm\s+-rf").unwrap(),
            Regex::new(r"\$\(").unwrap(),
            Regex::new(r"`[^`]*`").unwrap(),
            Regex::new(r"&&\s*rm").unwrap(),
            Regex::new(r"\|\s*bash").unwrap(),
        ];
        
        IntrusionDetector {
            clients: Arc::new(Mutex::new(HashMap::new())),
            alert_threshold,
            time_window,
            sql_patterns,
            cmd_patterns,
        }
    }
    
    fn current_timestamp() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs()
    }
    
    fn log_alert(&self, alert_type: &str, client_id: &str, details: &str) {
        let timestamp = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S");
        eprintln!("🚨 [{timestamp}] ALERT: {alert_type} - Client: {client_id} - {details}");
        
        // In production, write to file or send to SIEM
        std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open("/var/log/mqtt_ids.log")
            .and_then(|mut file| {
                use std::io::Write;
                writeln!(file, "[{timestamp}] ALERT: {alert_type} - Client: {client_id} - {details}")
            })
            .ok();
    }
    
    fn check_rate_anomaly(&self, client_id: &str) -> bool {
        let mut clients = self.clients.lock().unwrap();
        let now = Self::current_timestamp();
        
        let stats = clients.entry(client_id.to_string())
            .or_insert_with(|| ClientStats {
                client_id: client_id.to_string(),
                message_count: 0,
                last_reset: now,
                failed_auth_attempts: 0,
                last_seen: now,
            });
        
        // Reset counter if time window elapsed
        if now - stats.last_reset >= self.time_window {
            stats.message_count = 0;
            stats.last_reset = now;
        }
        
        stats.message_count += 1;
        stats.last_seen = now;
        
        // Check threshold
        if stats.message_count > self.alert_threshold {
            self.log_alert(
                "RATE_ANOMALY",
                client_id,
                &format!("Rate limit exceeded: {} msgs/{} sec", 
                        stats.message_count, self.time_window)
            );
            return true;
        }
        
        false
    }
    
    fn check_payload_threat(&self, client_id: &str, payload: &[u8]) -> bool {
        let payload_str = String::from_utf8_lossy(payload);
        
        // Check for SQL injection
        for pattern in &self.sql_patterns {
            if pattern.is_match(&payload_str) {
                self.log_alert(
                    "SQL_INJECTION",
                    client_id,
                    "Potential SQL injection detected in payload"
                );
                return true;
            }
        }
        
        // Check for command injection
        for pattern in &self.cmd_patterns {
            if pattern.is_match(&payload_str) {
                self.log_alert(
                    "CMD_INJECTION",
                    client_id,
                    "Potential command injection detected"
                );
                return true;
            }
        }
        
        false
    }
    
    fn check_sensitive_topic(&self, client_id: &str, topic: &str) -> bool {
        let sensitive_keywords = ["admin", "config", "system", "root"];
        
        for keyword in &sensitive_keywords {
            if topic.to_lowercase().contains(keyword) {
                self.log_alert(
                    "SENSITIVE_TOPIC",
                    client_id,
                    &format!("Access to sensitive topic: {topic}")
                );
                return true;
            }
        }
        
        false
    }
    
    fn inspect_message(&self, client_id: &str, topic: &str, payload: &[u8]) {
        // Check rate anomalies
        self.check_rate_anomaly(client_id);
        
        // Check payload threats
        self.check_payload_threat(client_id, payload);
        
        // Check sensitive topics
        self.check_sensitive_topic(client_id, topic);
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("mqtt_ids_monitor", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    let detector = Arc::new(IntrusionDetector::new(100, 1));
    
    // Subscribe to all topics
    client.subscribe("#", QoS::AtMostOnce).await?;
    
    println!("🔒 MQTT IDS Monitor started...");
    
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let client_id = "client_123"; // Extract from properties in production
                let topic = &publish.topic;
                let payload = &publish.payload;
                
                detector.inspect_message(client_id, topic, payload);
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Connection error: {e}");
                time::sleep(Duration::from_secs(5)).await;
            }
        }
    }
}
```

## Summary

**Intrusion Detection for MQTT** is essential for securing IoT deployments against cyber threats. Key aspects include:

- **Traffic Monitoring**: Analyzing message rates, connection patterns, and protocol behavior to identify anomalies
- **Threat Detection**: Identifying SQL/command injection, unauthorized access, brute-force attacks, and suspicious payloads
- **Rate Limiting**: Detecting and preventing denial-of-service attacks through message rate analysis
- **Real-time Alerting**: Logging security events and notifying administrators of potential threats
- **Behavioral Analysis**: Establishing baselines and detecting deviations from normal operation

The C/C++ implementation demonstrates low-level monitoring with efficient statistics tracking, while the Rust implementation provides memory-safe pattern matching with async processing. Both examples show rate anomaly detection, payload inspection, and alert logging—foundational components for production IDS systems that should be extended with machine learning, distributed monitoring, and integration with Security Information and Event Management (SIEM) systems.