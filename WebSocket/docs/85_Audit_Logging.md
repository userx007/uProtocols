# Audit Logging in WebSocket Systems

## Overview

Audit logging in WebSocket systems involves creating comprehensive, tamper-evident records of all significant events, security-relevant actions, and data access patterns. This is critical for security compliance (SOX, HIPAA, PCI-DSS, GDPR), forensic analysis, incident response, and detecting unauthorized access or suspicious behavior.

## Key Components

### 1. **What to Log**
- **Connection Events**: Establish, close, failures, authentication attempts
- **Message Activity**: Send/receive timestamps, message types, sizes, directions
- **Security Events**: Authentication/authorization success/failure, policy violations, rate limit triggers
- **Data Access**: Who accessed what data, when, and from where
- **Administrative Actions**: Configuration changes, user management, permission modifications
- **Errors and Exceptions**: Protocol violations, parsing errors, system failures

### 2. **Log Entry Structure**
- **Timestamp**: High-precision time (microseconds/nanoseconds)
- **Event Type**: Categorized event classification
- **Actor/Subject**: User ID, session ID, IP address, client info
- **Action**: Specific operation performed
- **Resource**: Target object/data affected
- **Outcome**: Success, failure, error code
- **Context**: Additional metadata (geolocation, device fingerprint, etc.)

### 3. **Compliance Requirements**
- **Immutability**: Logs cannot be altered after creation
- **Integrity**: Cryptographic verification (hashing, signing)
- **Retention**: Defined periods based on regulations
- **Access Control**: Who can read/export logs
- **Privacy**: PII protection, data minimization

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Audit log levels
typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_SECURITY,
    LOG_CRITICAL
} LogLevel;

// Audit event types
typedef enum {
    EVENT_CONNECTION_OPEN,
    EVENT_CONNECTION_CLOSE,
    EVENT_AUTH_SUCCESS,
    EVENT_AUTH_FAILURE,
    EVENT_MESSAGE_SENT,
    EVENT_MESSAGE_RECEIVED,
    EVENT_ACCESS_DENIED,
    EVENT_RATE_LIMIT_EXCEEDED,
    EVENT_ADMIN_ACTION
} EventType;

// Audit log entry structure
typedef struct {
    uint64_t id;
    struct timespec timestamp;
    LogLevel level;
    EventType event_type;
    char user_id[64];
    char session_id[128];
    char ip_address[46];  // IPv6 compatible
    char action[256];
    char resource[256];
    int outcome;          // 0 = success, non-zero = error code
    char details[1024];
    unsigned char hash[SHA256_DIGEST_LENGTH];  // Integrity hash
    unsigned char prev_hash[SHA256_DIGEST_LENGTH];  // Chain to previous entry
} AuditLogEntry;

// Thread-safe audit logger
typedef struct {
    FILE *log_file;
    pthread_mutex_t mutex;
    uint64_t entry_count;
    unsigned char last_hash[SHA256_DIGEST_LENGTH];
    int rotation_size_mb;
    char base_filename[256];
} AuditLogger;

// Initialize audit logger
AuditLogger* audit_logger_init(const char *filename, int rotation_size_mb) {
    AuditLogger *logger = (AuditLogger*)malloc(sizeof(AuditLogger));
    if (!logger) return NULL;
    
    strncpy(logger->base_filename, filename, sizeof(logger->base_filename) - 1);
    logger->log_file = fopen(filename, "ab");  // Append mode
    if (!logger->log_file) {
        free(logger);
        return NULL;
    }
    
    pthread_mutex_init(&logger->mutex, NULL);
    logger->entry_count = 0;
    logger->rotation_size_mb = rotation_size_mb;
    memset(logger->last_hash, 0, SHA256_DIGEST_LENGTH);
    
    return logger;
}

// Compute hash chain for integrity
void compute_entry_hash(AuditLogEntry *entry) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    
    // Hash all fields except the hash itself
    SHA256_Update(&ctx, &entry->id, sizeof(entry->id));
    SHA256_Update(&ctx, &entry->timestamp, sizeof(entry->timestamp));
    SHA256_Update(&ctx, &entry->level, sizeof(entry->level));
    SHA256_Update(&ctx, &entry->event_type, sizeof(entry->event_type));
    SHA256_Update(&ctx, entry->user_id, strlen(entry->user_id));
    SHA256_Update(&ctx, entry->session_id, strlen(entry->session_id));
    SHA256_Update(&ctx, entry->ip_address, strlen(entry->ip_address));
    SHA256_Update(&ctx, entry->action, strlen(entry->action));
    SHA256_Update(&ctx, entry->resource, strlen(entry->resource));
    SHA256_Update(&ctx, &entry->outcome, sizeof(entry->outcome));
    SHA256_Update(&ctx, entry->details, strlen(entry->details));
    SHA256_Update(&ctx, entry->prev_hash, SHA256_DIGEST_LENGTH);
    
    SHA256_Final(entry->hash, &ctx);
}

// Log an audit event
int audit_log(AuditLogger *logger, LogLevel level, EventType event_type,
              const char *user_id, const char *session_id, const char *ip_addr,
              const char *action, const char *resource, int outcome,
              const char *details) {
    
    pthread_mutex_lock(&logger->mutex);
    
    AuditLogEntry entry;
    memset(&entry, 0, sizeof(entry));
    
    // Populate entry
    entry.id = ++logger->entry_count;
    clock_gettime(CLOCK_REALTIME, &entry.timestamp);
    entry.level = level;
    entry.event_type = event_type;
    entry.outcome = outcome;
    
    strncpy(entry.user_id, user_id ? user_id : "SYSTEM", sizeof(entry.user_id) - 1);
    strncpy(entry.session_id, session_id ? session_id : "", sizeof(entry.session_id) - 1);
    strncpy(entry.ip_address, ip_addr ? ip_addr : "", sizeof(entry.ip_address) - 1);
    strncpy(entry.action, action, sizeof(entry.action) - 1);
    strncpy(entry.resource, resource ? resource : "", sizeof(entry.resource) - 1);
    strncpy(entry.details, details ? details : "", sizeof(entry.details) - 1);
    
    // Chain to previous entry
    memcpy(entry.prev_hash, logger->last_hash, SHA256_DIGEST_LENGTH);
    
    // Compute integrity hash
    compute_entry_hash(&entry);
    
    // Write to log file
    size_t written = fwrite(&entry, sizeof(AuditLogEntry), 1, logger->log_file);
    fflush(logger->log_file);
    
    // Update last hash for chaining
    memcpy(logger->last_hash, entry.hash, SHA256_DIGEST_LENGTH);
    
    // Check for log rotation
    long file_size = ftell(logger->log_file);
    if (file_size > (logger->rotation_size_mb * 1024 * 1024)) {
        char new_filename[512];
        time_t now = time(NULL);
        snprintf(new_filename, sizeof(new_filename), "%s.%ld", 
                 logger->base_filename, now);
        
        fclose(logger->log_file);
        rename(logger->base_filename, new_filename);
        logger->log_file = fopen(logger->base_filename, "ab");
    }
    
    pthread_mutex_unlock(&logger->mutex);
    
    return written == 1 ? 0 : -1;
}

// Verify log integrity
int verify_log_integrity(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) return -1;
    
    AuditLogEntry entry, prev_entry;
    unsigned char computed_hash[SHA256_DIGEST_LENGTH];
    int is_first = 1;
    int verified = 1;
    
    while (fread(&entry, sizeof(AuditLogEntry), 1, file) == 1) {
        // Verify hash chain
        if (!is_first) {
            if (memcmp(entry.prev_hash, prev_entry.hash, SHA256_DIGEST_LENGTH) != 0) {
                printf("Hash chain broken at entry %lu\n", entry.id);
                verified = 0;
                break;
            }
        }
        
        // Verify entry hash
        unsigned char saved_hash[SHA256_DIGEST_LENGTH];
        memcpy(saved_hash, entry.hash, SHA256_DIGEST_LENGTH);
        compute_entry_hash(&entry);
        
        if (memcmp(saved_hash, entry.hash, SHA256_DIGEST_LENGTH) != 0) {
            printf("Entry %lu has been tampered with\n", entry.id);
            verified = 0;
            break;
        }
        
        prev_entry = entry;
        is_first = 0;
    }
    
    fclose(file);
    return verified ? 0 : -1;
}

// Example: WebSocket connection audit
void audit_ws_connection(AuditLogger *logger, const char *user_id, 
                         const char *session_id, struct sockaddr_in *addr,
                         int success) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr->sin_addr), ip_str, INET_ADDRSTRLEN);
    
    char details[512];
    snprintf(details, sizeof(details), "Port: %d, Protocol: WebSocket",
             ntohs(addr->sin_port));
    
    audit_log(logger, 
              success ? LOG_INFO : LOG_SECURITY,
              success ? EVENT_CONNECTION_OPEN : EVENT_AUTH_FAILURE,
              user_id,
              session_id,
              ip_str,
              success ? "CONNECT" : "CONNECT_FAILED",
              "websocket://server/ws",
              success ? 0 : 401,
              details);
}

// Cleanup
void audit_logger_destroy(AuditLogger *logger) {
    if (logger) {
        pthread_mutex_lock(&logger->mutex);
        if (logger->log_file) {
            fclose(logger->log_file);
        }
        pthread_mutex_unlock(&logger->mutex);
        pthread_mutex_destroy(&logger->mutex);
        free(logger);
    }
}

// Usage example
int main() {
    AuditLogger *logger = audit_logger_init("audit.log", 100);
    if (!logger) {
        fprintf(stderr, "Failed to initialize audit logger\n");
        return 1;
    }
    
    // Simulate WebSocket events
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.100", &client_addr.sin_addr);
    client_addr.sin_port = htons(54321);
    
    audit_ws_connection(logger, "user123", "sess-abc-def", &client_addr, 1);
    
    audit_log(logger, LOG_INFO, EVENT_MESSAGE_RECEIVED,
              "user123", "sess-abc-def", "192.168.1.100",
              "RECEIVE", "/api/data", 0,
              "Message size: 1024 bytes, Type: JSON");
    
    audit_log(logger, LOG_SECURITY, EVENT_ACCESS_DENIED,
              "user456", "sess-xyz-123", "10.0.0.50",
              "ACCESS", "/admin/config", 403,
              "Insufficient permissions");
    
    // Verify integrity
    printf("Verifying log integrity...\n");
    if (verify_log_integrity("audit.log") == 0) {
        printf("Log integrity verified successfully\n");
    } else {
        printf("Log integrity check failed!\n");
    }
    
    audit_logger_destroy(logger);
    return 0;
}
```

## Rust Implementation

```rust
use std::fs::{File, OpenOptions};
use std::io::{Write, Read, Seek, SeekFrom};
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use serde::{Serialize, Deserialize};
use sha2::{Sha256, Digest};
use chrono::{DateTime, Utc};
use std::net::IpAddr;

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum LogLevel {
    Info,
    Warning,
    Error,
    Security,
    Critical,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub enum EventType {
    ConnectionOpen,
    ConnectionClose,
    AuthSuccess,
    AuthFailure,
    MessageSent,
    MessageReceived,
    AccessDenied,
    RateLimitExceeded,
    AdminAction,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AuditLogEntry {
    pub id: u64,
    pub timestamp: DateTime<Utc>,
    pub level: LogLevel,
    pub event_type: EventType,
    pub user_id: String,
    pub session_id: String,
    pub ip_address: Option<IpAddr>,
    pub action: String,
    pub resource: String,
    pub outcome: i32,  // 0 = success, non-zero = error code
    pub details: String,
    #[serde(with = "hex")]
    pub hash: [u8; 32],
    #[serde(with = "hex")]
    pub prev_hash: [u8; 32],
}

mod hex {
    use serde::{Deserialize, Deserializer, Serializer};
    
    pub fn serialize<S>(bytes: &[u8; 32], serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(&hex::encode(bytes))
    }
    
    pub fn deserialize<'de, D>(deserializer: D) -> Result<[u8; 32], D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        let bytes = hex::decode(&s).map_err(serde::de::Error::custom)?;
        let mut array = [0u8; 32];
        array.copy_from_slice(&bytes);
        Ok(array)
    }
}

impl AuditLogEntry {
    fn compute_hash(&self) -> [u8; 32] {
        let mut hasher = Sha256::new();
        
        hasher.update(self.id.to_le_bytes());
        hasher.update(self.timestamp.timestamp().to_le_bytes());
        hasher.update(&[self.level as u8]);
        hasher.update(&[self.event_type as u8]);
        hasher.update(self.user_id.as_bytes());
        hasher.update(self.session_id.as_bytes());
        
        if let Some(ip) = self.ip_address {
            hasher.update(ip.to_string().as_bytes());
        }
        
        hasher.update(self.action.as_bytes());
        hasher.update(self.resource.as_bytes());
        hasher.update(self.outcome.to_le_bytes());
        hasher.update(self.details.as_bytes());
        hasher.update(&self.prev_hash);
        
        hasher.finalize().into()
    }
}

pub struct AuditLogger {
    file: Arc<Mutex<File>>,
    entry_count: Arc<Mutex<u64>>,
    last_hash: Arc<Mutex<[u8; 32]>>,
    rotation_size_mb: usize,
    base_filename: String,
}

impl AuditLogger {
    pub fn new(filename: &str, rotation_size_mb: usize) -> std::io::Result<Self> {
        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(filename)?;
        
        Ok(Self {
            file: Arc::new(Mutex::new(file)),
            entry_count: Arc::new(Mutex::new(0)),
            last_hash: Arc::new(Mutex::new([0u8; 32])),
            rotation_size_mb,
            base_filename: filename.to_string(),
        })
    }
    
    pub fn log(
        &self,
        level: LogLevel,
        event_type: EventType,
        user_id: &str,
        session_id: &str,
        ip_address: Option<IpAddr>,
        action: &str,
        resource: &str,
        outcome: i32,
        details: &str,
    ) -> std::io::Result<()> {
        let mut file = self.file.lock().unwrap();
        let mut count = self.entry_count.lock().unwrap();
        let mut last_hash = self.last_hash.lock().unwrap();
        
        *count += 1;
        
        let mut entry = AuditLogEntry {
            id: *count,
            timestamp: Utc::now(),
            level,
            event_type,
            user_id: user_id.to_string(),
            session_id: session_id.to_string(),
            ip_address,
            action: action.to_string(),
            resource: resource.to_string(),
            outcome,
            details: details.to_string(),
            hash: [0u8; 32],
            prev_hash: *last_hash,
        };
        
        // Compute integrity hash
        entry.hash = entry.compute_hash();
        
        // Write as JSON lines format for easier parsing
        let json = serde_json::to_string(&entry)?;
        writeln!(file, "{}", json)?;
        file.flush()?;
        
        // Update last hash
        *last_hash = entry.hash;
        
        // Check for rotation
        let file_size = file.metadata()?.len();
        if file_size > (self.rotation_size_mb * 1024 * 1024) as u64 {
            drop(file); // Release lock before rotation
            self.rotate_log()?;
        }
        
        Ok(())
    }
    
    fn rotate_log(&self) -> std::io::Result<()> {
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        let new_name = format!("{}.{}", self.base_filename, timestamp);
        std::fs::rename(&self.base_filename, new_name)?;
        
        let new_file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.base_filename)?;
        
        *self.file.lock().unwrap() = new_file;
        
        Ok(())
    }
    
    pub fn verify_integrity(filename: &str) -> std::io::Result<bool> {
        let mut file = File::open(filename)?;
        let mut contents = String::new();
        file.read_to_string(&mut contents)?;
        
        let mut prev_entry: Option<AuditLogEntry> = None;
        
        for (line_num, line) in contents.lines().enumerate() {
            let entry: AuditLogEntry = serde_json::from_str(line)
                .map_err(|e| std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    format!("Line {}: {}", line_num + 1, e)
                ))?;
            
            // Verify hash chain
            if let Some(prev) = &prev_entry {
                if entry.prev_hash != prev.hash {
                    eprintln!("Hash chain broken at entry {}", entry.id);
                    return Ok(false);
                }
            }
            
            // Verify entry hash
            let saved_hash = entry.hash;
            let computed_hash = entry.compute_hash();
            
            if saved_hash != computed_hash {
                eprintln!("Entry {} has been tampered with", entry.id);
                return Ok(false);
            }
            
            prev_entry = Some(entry);
        }
        
        Ok(true)
    }
}

// WebSocket-specific audit helpers
pub struct WebSocketAuditor {
    logger: Arc<AuditLogger>,
}

impl WebSocketAuditor {
    pub fn new(logger: Arc<AuditLogger>) -> Self {
        Self { logger }
    }
    
    pub fn audit_connection(
        &self,
        user_id: &str,
        session_id: &str,
        ip: IpAddr,
        success: bool,
        port: u16,
    ) -> std::io::Result<()> {
        let details = format!("Port: {}, Protocol: WebSocket", port);
        
        self.logger.log(
            if success { LogLevel::Info } else { LogLevel::Security },
            if success { EventType::ConnectionOpen } else { EventType::AuthFailure },
            user_id,
            session_id,
            Some(ip),
            if success { "CONNECT" } else { "CONNECT_FAILED" },
            "websocket://server/ws",
            if success { 0 } else { 401 },
            &details,
        )
    }
    
    pub fn audit_message(
        &self,
        user_id: &str,
        session_id: &str,
        ip: IpAddr,
        direction: &str,  // "SEND" or "RECEIVE"
        size: usize,
        msg_type: &str,
    ) -> std::io::Result<()> {
        let details = format!("Size: {} bytes, Type: {}", size, msg_type);
        
        self.logger.log(
            LogLevel::Info,
            if direction == "SEND" { 
                EventType::MessageSent 
            } else { 
                EventType::MessageReceived 
            },
            user_id,
            session_id,
            Some(ip),
            direction,
            "/ws/messages",
            0,
            &details,
        )
    }
    
    pub fn audit_access_denied(
        &self,
        user_id: &str,
        session_id: &str,
        ip: IpAddr,
        resource: &str,
        reason: &str,
    ) -> std::io::Result<()> {
        self.logger.log(
            LogLevel::Security,
            EventType::AccessDenied,
            user_id,
            session_id,
            Some(ip),
            "ACCESS",
            resource,
            403,
            reason,
        )
    }
}

// Example usage
fn main() -> std::io::Result<()> {
    let logger = Arc::new(AuditLogger::new("audit.log", 100)?);
    let auditor = WebSocketAuditor::new(logger.clone());
    
    // Simulate WebSocket events
    let user_ip: IpAddr = "192.168.1.100".parse().unwrap();
    
    auditor.audit_connection("user123", "sess-abc-def", user_ip, true, 8080)?;
    
    auditor.audit_message(
        "user123",
        "sess-abc-def",
        user_ip,
        "RECEIVE",
        1024,
        "JSON",
    )?;
    
    let admin_ip: IpAddr = "10.0.0.50".parse().unwrap();
    auditor.audit_access_denied(
        "user456",
        "sess-xyz-123",
        admin_ip,
        "/admin/config",
        "Insufficient permissions",
    )?;
    
    // Verify integrity
    println!("Verifying log integrity...");
    match AuditLogger::verify_integrity("audit.log") {
        Ok(true) => println!("✓ Log integrity verified successfully"),
        Ok(false) => println!("✗ Log integrity check failed!"),
        Err(e) => println!("Error verifying log: {}", e),
    }
    
    Ok(())
}
```

## Summary

**Audit logging for WebSocket systems** provides comprehensive, tamper-evident records of all security-relevant events, which is essential for:

- **Compliance**: Meeting regulatory requirements (HIPAA, SOX, PCI-DSS, GDPR)
- **Security**: Detecting breaches, unauthorized access, and policy violations
- **Forensics**: Investigating incidents and reconstructing event timelines
- **Accountability**: Proving who did what and when

**Key features** include:
- **Hash chaining** for tamper-evidence (blockchain-style integrity)
- **Structured logging** with consistent fields for analysis
- **Thread-safe operations** for concurrent WebSocket connections
- **Log rotation** to manage disk space
- **Multiple severity levels** for filtering and alerting
- **Cryptographic verification** to detect tampering

Both implementations demonstrate production-ready patterns with integrity verification, proper serialization, and WebSocket-specific event tracking. The Rust version leverages type safety and modern serialization, while the C version shows low-level control with OpenSSL for cryptographic operations.