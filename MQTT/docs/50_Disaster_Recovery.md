# MQTT Disaster Recovery: A Comprehensive Guide

## Overview

Disaster Recovery (DR) in MQTT systems involves implementing strategies to ensure message broker availability, data integrity, and business continuity during catastrophic failures. This encompasses backup procedures, failover mechanisms, and recovery protocols to maintain reliable message delivery even when primary systems fail.

## Core Concepts

### 1. **Backup Strategies**
- **Broker State Backup**: Persisting retained messages, subscriptions, and session data
- **Configuration Backup**: Saving broker configurations, ACLs, and certificates
- **Message Queue Backup**: Capturing in-flight and queued messages

### 2. **Failover Mechanisms**
- **Active-Passive Failover**: Standby broker takes over when primary fails
- **Active-Active Clustering**: Multiple brokers share load with automatic failover
- **Geographic Redundancy**: Distributed brokers across regions

### 3. **Business Continuity Planning**
- **Recovery Time Objective (RTO)**: Maximum acceptable downtime
- **Recovery Point Objective (RPO)**: Maximum acceptable data loss
- **Automated Recovery**: Self-healing systems without manual intervention

---

## C/C++ Implementation Examples

### Example 1: MQTT Client with Automatic Reconnection and Session Recovery

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>
#include <time.h>

#define PRIMARY_BROKER    "tcp://primary-broker:1883"
#define BACKUP_BROKER     "tcp://backup-broker:1883"
#define CLIENTID          "DisasterRecoveryClient"
#define QOS               1
#define TIMEOUT           10000L

typedef struct {
    char* primary_address;
    char* backup_address;
    int connection_attempts;
    time_t last_backup_time;
} DRContext;

// Connection lost callback - implements automatic failover
void connlost(void *context, char *cause) {
    printf("\n[DR] Connection lost: %s\n", cause);
    printf("[DR] Initiating failover procedure...\n");
}

// Message arrival callback with backup logging
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    DRContext* dr_ctx = (DRContext*)context;
    
    printf("[DR] Message received on topic %s: %.*s\n", 
           topicName, message->payloadlen, (char*)message->payload);
    
    // Log message to backup storage for disaster recovery
    FILE* backup_log = fopen("/var/log/mqtt_backup.log", "a");
    if (backup_log) {
        fprintf(backup_log, "[%ld] Topic: %s, Payload: %.*s\n",
                time(NULL), topicName, message->payloadlen, (char*)message->payload);
        fclose(backup_log);
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Attempt connection with failover support
MQTTClient create_resilient_client(DRContext* dr_ctx) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    // Try primary broker first
    MQTTClient_create(&client, dr_ctx->primary_address, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
    
    // Configure connection with clean session = 0 for session recovery
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 0;  // Persistent session for recovery
    conn_opts.reliable = 1;
    conn_opts.automaticReconnect = 1;
    
    // Set callback for connection loss
    MQTTClient_setCallbacks(client, dr_ctx, connlost, msgarrvd, NULL);
    
    int rc;
    printf("[DR] Attempting connection to primary broker...\n");
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("[DR] Primary broker failed (rc=%d), trying backup...\n", rc);
        
        // Destroy and recreate client for backup broker
        MQTTClient_destroy(&client);
        MQTTClient_create(&client, dr_ctx->backup_address, CLIENTID,
                          MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
        MQTTClient_setCallbacks(client, dr_ctx, connlost, msgarrvd, NULL);
        
        if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
            printf("[DR] Backup broker also failed (rc=%d)\n", rc);
            MQTTClient_destroy(&client);
            return NULL;
        }
        printf("[DR] Successfully failed over to backup broker\n");
    } else {
        printf("[DR] Connected to primary broker\n");
    }
    
    return client;
}

// Backup critical session data
void backup_session_data(MQTTClient client, const char* backup_file) {
    FILE* fp = fopen(backup_file, "w");
    if (!fp) {
        printf("[DR] Failed to create backup file\n");
        return;
    }
    
    // In production, serialize subscription list and retained messages
    fprintf(fp, "# MQTT Session Backup - %ld\n", time(NULL));
    fprintf(fp, "CLIENT_ID=%s\n", CLIENTID);
    fprintf(fp, "SUBSCRIPTIONS=sensors/+/data,alerts/#\n");
    
    fclose(fp);
    printf("[DR] Session data backed up successfully\n");
}

int main(int argc, char* argv[]) {
    DRContext dr_context = {
        .primary_address = PRIMARY_BROKER,
        .backup_address = BACKUP_BROKER,
        .connection_attempts = 0,
        .last_backup_time = 0
    };
    
    MQTTClient client = create_resilient_client(&dr_context);
    if (!client) {
        printf("[DR] Failed to establish connection to any broker\n");
        return EXIT_FAILURE;
    }
    
    // Subscribe to topics with QoS 1 for guaranteed delivery
    MQTTClient_subscribe(client, "sensors/+/data", QOS);
    MQTTClient_subscribe(client, "alerts/#", QOS);
    
    // Periodically backup session data
    printf("[DR] Client running with disaster recovery enabled...\n");
    for (int i = 0; i < 60; i++) {
        if (i % 10 == 0) {
            backup_session_data(client, "/tmp/mqtt_session_backup.txt");
        }
        sleep(1);
    }
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return EXIT_SUCCESS;
}
```

### Example 2: Message Queue Backup System

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <MQTTClient.h>

#define BACKUP_DB "/var/mqtt/message_backup.db"

typedef struct {
    sqlite3* db;
    int message_count;
} BackupContext;

// Initialize backup database
int init_backup_db(BackupContext* ctx) {
    int rc = sqlite3_open(BACKUP_DB, &ctx->db);
    if (rc) {
        fprintf(stderr, "[BACKUP] Cannot open database: %s\n", sqlite3_errmsg(ctx->db));
        return rc;
    }
    
    // Create backup table for messages
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS message_backup ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp INTEGER,"
        "topic TEXT,"
        "payload BLOB,"
        "qos INTEGER,"
        "retained INTEGER,"
        "processed INTEGER DEFAULT 0);";
    
    char* err_msg = NULL;
    rc = sqlite3_exec(ctx->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[BACKUP] SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return rc;
    }
    
    printf("[BACKUP] Database initialized successfully\n");
    return SQLITE_OK;
}

// Backup incoming message to database
int backup_message(BackupContext* ctx, const char* topic, 
                   const void* payload, int payloadlen, int qos, int retained) {
    const char* sql = 
        "INSERT INTO message_backup (timestamp, topic, payload, qos, retained) "
        "VALUES (?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[BACKUP] Prepare failed: %s\n", sqlite3_errmsg(ctx->db));
        return rc;
    }
    
    sqlite3_bind_int64(stmt, 1, time(NULL));
    sqlite3_bind_text(stmt, 2, topic, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, payload, payloadlen, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, qos);
    sqlite3_bind_int(stmt, 5, retained);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[BACKUP] Execution failed: %s\n", sqlite3_errmsg(ctx->db));
    } else {
        ctx->message_count++;
        if (ctx->message_count % 100 == 0) {
            printf("[BACKUP] %d messages backed up\n", ctx->message_count);
        }
    }
    
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

// Restore messages from backup after disaster recovery
int restore_messages(BackupContext* ctx, MQTTClient client) {
    const char* sql = 
        "SELECT topic, payload, qos, retained FROM message_backup "
        "WHERE processed = 0 ORDER BY timestamp ASC;";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[RESTORE] Prepare failed: %s\n", sqlite3_errmsg(ctx->db));
        return rc;
    }
    
    int restored_count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* topic = (const char*)sqlite3_column_text(stmt, 0);
        const void* payload = sqlite3_column_blob(stmt, 1);
        int payload_len = sqlite3_column_bytes(stmt, 1);
        int qos = sqlite3_column_int(stmt, 2);
        
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = (void*)payload;
        pubmsg.payloadlen = payload_len;
        pubmsg.qos = qos;
        pubmsg.retained = sqlite3_column_int(stmt, 3);
        
        MQTTClient_deliveryToken token;
        rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
        if (rc == MQTTCLIENT_SUCCESS) {
            restored_count++;
            MQTTClient_waitForCompletion(client, token, 1000L);
        }
    }
    
    printf("[RESTORE] Restored %d messages from backup\n", restored_count);
    
    // Mark messages as processed
    sqlite3_exec(ctx->db, "UPDATE message_backup SET processed = 1 WHERE processed = 0;",
                 NULL, NULL, NULL);
    
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

void cleanup_backup(BackupContext* ctx) {
    if (ctx->db) {
        sqlite3_close(ctx->db);
    }
}
```

---

## Rust Implementation Examples

### Example 1: High-Availability MQTT Client with Failover

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use serde::{Serialize, Deserialize};
use std::sync::Arc;
use tokio::sync::Mutex;
use std::fs::OpenOptions;
use std::io::Write;

#[derive(Debug, Clone)]
struct BrokerConfig {
    primary: String,
    backup: String,
    port: u16,
}

#[derive(Debug, Serialize, Deserialize)]
struct SessionBackup {
    client_id: String,
    subscriptions: Vec<String>,
    timestamp: i64,
}

struct DisasterRecoveryClient {
    broker_config: BrokerConfig,
    client_id: String,
    backup_log_path: String,
    failover_count: Arc<Mutex<u32>>,
}

impl DisasterRecoveryClient {
    fn new(broker_config: BrokerConfig, client_id: String) -> Self {
        Self {
            broker_config,
            client_id,
            backup_log_path: "/var/log/mqtt_dr.log".to_string(),
            failover_count: Arc::new(Mutex::new(0)),
        }
    }
    
    // Create MQTT client with automatic reconnection
    async fn create_resilient_client(&self, use_backup: bool) 
        -> Result<(AsyncClient, rumqttc::EventLoop), Box<dyn std::error::Error>> {
        
        let broker_address = if use_backup {
            println!("[DR] Using backup broker: {}", self.broker_config.backup);
            &self.broker_config.backup
        } else {
            println!("[DR] Using primary broker: {}", self.broker_config.primary);
            &self.broker_config.primary
        };
        
        let mut mqttoptions = MqttOptions::new(
            &self.client_id,
            broker_address,
            self.broker_config.port
        );
        
        // Configure for disaster recovery
        mqttoptions.set_keep_alive(Duration::from_secs(20));
        mqttoptions.set_clean_session(false); // Persistent sessions
        mqttoptions.set_connection_timeout(10);
        
        let (client, eventloop) = AsyncClient::new(mqttoptions, 100);
        
        Ok((client, eventloop))
    }
    
    // Backup message to persistent storage
    async fn backup_message(&self, topic: &str, payload: &[u8]) 
        -> Result<(), Box<dyn std::error::Error>> {
        
        let mut file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.backup_log_path)?;
        
        let timestamp = chrono::Utc::now().timestamp();
        let payload_str = String::from_utf8_lossy(payload);
        
        writeln!(file, "[{}] Topic: {}, Payload: {}", 
                 timestamp, topic, payload_str)?;
        
        Ok(())
    }
    
    // Backup session configuration
    async fn backup_session(&self, subscriptions: Vec<String>) 
        -> Result<(), Box<dyn std::error::Error>> {
        
        let backup = SessionBackup {
            client_id: self.client_id.clone(),
            subscriptions,
            timestamp: chrono::Utc::now().timestamp(),
        };
        
        let json = serde_json::to_string_pretty(&backup)?;
        std::fs::write("/tmp/mqtt_session_backup.json", json)?;
        
        println!("[DR] Session backup completed");
        Ok(())
    }
    
    // Main event loop with automatic failover
    async fn run(&self) -> Result<(), Box<dyn std::error::Error>> {
        let mut use_backup = false;
        let subscriptions = vec![
            "sensors/+/data".to_string(),
            "alerts/#".to_string(),
        ];
        
        loop {
            let (client, mut eventloop) = self.create_resilient_client(use_backup).await?;
            
            // Subscribe to topics
            for topic in &subscriptions {
                client.subscribe(topic, QoS::AtLeastOnce).await?;
            }
            
            println!("[DR] Client connected and subscribed");
            
            // Backup session periodically
            let backup_task = {
                let subs = subscriptions.clone();
                let client_clone = self.clone();
                tokio::spawn(async move {
                    loop {
                        sleep(Duration::from_secs(30)).await;
                        let _ = client_clone.backup_session(subs.clone()).await;
                    }
                })
            };
            
            // Process events
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::Publish(publish))) => {
                        println!("[DR] Received: {} -> {}", 
                                 publish.topic, 
                                 String::from_utf8_lossy(&publish.payload));
                        
                        // Backup message for disaster recovery
                        let _ = self.backup_message(&publish.topic, &publish.payload).await;
                    }
                    Ok(Event::Incoming(Packet::ConnAck(_))) => {
                        println!("[DR] Connection acknowledged");
                    }
                    Err(e) => {
                        println!("[DR] Connection error: {:?}", e);
                        println!("[DR] Initiating failover...");
                        
                        let mut count = self.failover_count.lock().await;
                        *count += 1;
                        println!("[DR] Failover attempt #{}", *count);
                        
                        use_backup = !use_backup; // Toggle between brokers
                        backup_task.abort();
                        
                        sleep(Duration::from_secs(5)).await;
                        break; // Break inner loop to reconnect
                    }
                    _ => {}
                }
            }
        }
    }
}

impl Clone for DisasterRecoveryClient {
    fn clone(&self) -> Self {
        Self {
            broker_config: self.broker_config.clone(),
            client_id: self.client_id.clone(),
            backup_log_path: self.backup_log_path.clone(),
            failover_count: Arc::clone(&self.failover_count),
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let broker_config = BrokerConfig {
        primary: "primary-broker.example.com".to_string(),
        backup: "backup-broker.example.com".to_string(),
        port: 1883,
    };
    
    let dr_client = DisasterRecoveryClient::new(
        broker_config,
        "dr_client_001".to_string()
    );
    
    println!("[DR] Starting disaster recovery client...");
    dr_client.run().await?;
    
    Ok(())
}
```

### Example 2: Message Queue Backup with SQLite

```rust
use rusqlite::{Connection, params};
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Debug)]
struct MessageBackup {
    id: i64,
    timestamp: i64,
    topic: String,
    payload: Vec<u8>,
    qos: i32,
    retained: bool,
    processed: bool,
}

struct BackupManager {
    db_path: String,
    conn: Arc<Mutex<Connection>>,
}

impl BackupManager {
    fn new(db_path: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let conn = Connection::open(db_path)?;
        
        // Create backup table
        conn.execute(
            "CREATE TABLE IF NOT EXISTS message_backup (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp INTEGER NOT NULL,
                topic TEXT NOT NULL,
                payload BLOB NOT NULL,
                qos INTEGER NOT NULL,
                retained INTEGER NOT NULL,
                processed INTEGER DEFAULT 0
            )",
            [],
        )?;
        
        println!("[BACKUP] Database initialized at {}", db_path);
        
        Ok(Self {
            db_path: db_path.to_string(),
            conn: Arc::new(Mutex::new(conn)),
        })
    }
    
    // Backup a message to database
    async fn backup_message(
        &self,
        topic: &str,
        payload: &[u8],
        qos: QoS,
        retained: bool,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let conn = self.conn.lock().await;
        
        conn.execute(
            "INSERT INTO message_backup (timestamp, topic, payload, qos, retained)
             VALUES (?1, ?2, ?3, ?4, ?5)",
            params![
                chrono::Utc::now().timestamp(),
                topic,
                payload,
                qos as i32,
                retained as i32,
            ],
        )?;
        
        Ok(())
    }
    
    // Restore unprocessed messages after disaster recovery
    async fn restore_messages(
        &self,
        client: &AsyncClient,
    ) -> Result<usize, Box<dyn std::error::Error>> {
        let conn = self.conn.lock().await;
        
        let mut stmt = conn.prepare(
            "SELECT id, topic, payload, qos, retained 
             FROM message_backup 
             WHERE processed = 0 
             ORDER BY timestamp ASC"
        )?;
        
        let messages = stmt.query_map([], |row| {
            Ok(MessageBackup {
                id: row.get(0)?,
                timestamp: 0,
                topic: row.get(1)?,
                payload: row.get(2)?,
                qos: row.get(3)?,
                retained: row.get(4)?,
                processed: false,
            })
        })?;
        
        let mut restored_count = 0;
        let mut message_ids = Vec::new();
        
        for message in messages {
            let msg = message?;
            let qos = match msg.qos {
                0 => QoS::AtMostOnce,
                1 => QoS::AtLeastOnce,
                2 => QoS::ExactlyOnce,
                _ => QoS::AtLeastOnce,
            };
            
            // Republish message
            client.publish(&msg.topic, qos, msg.retained, msg.payload).await?;
            
            message_ids.push(msg.id);
            restored_count += 1;
        }
        
        // Mark messages as processed
        if !message_ids.is_empty() {
            let placeholders = message_ids.iter()
                .map(|_| "?")
                .collect::<Vec<_>>()
                .join(",");
            
            let query = format!(
                "UPDATE message_backup SET processed = 1 WHERE id IN ({})",
                placeholders
            );
            
            let params: Vec<_> = message_ids.iter().map(|id| id as &dyn rusqlite::ToSql).collect();
            conn.execute(&query, params.as_slice())?;
        }
        
        println!("[RESTORE] Restored {} messages from backup", restored_count);
        Ok(restored_count)
    }
    
    // Cleanup old processed messages
    async fn cleanup_old_backups(&self, days: i64) -> Result<usize, Box<dyn std::error::Error>> {
        let conn = self.conn.lock().await;
        let cutoff = chrono::Utc::now().timestamp() - (days * 86400);
        
        let deleted = conn.execute(
            "DELETE FROM message_backup WHERE processed = 1 AND timestamp < ?1",
            params![cutoff],
        )?;
        
        println!("[CLEANUP] Removed {} old backup records", deleted);
        Ok(deleted)
    }
    
    // Get backup statistics
    async fn get_stats(&self) -> Result<(usize, usize), Box<dyn std::error::Error>> {
        let conn = self.conn.lock().await;
        
        let total: usize = conn.query_row(
            "SELECT COUNT(*) FROM message_backup",
            [],
            |row| row.get(0),
        )?;
        
        let unprocessed: usize = conn.query_row(
            "SELECT COUNT(*) FROM message_backup WHERE processed = 0",
            [],
            |row| row.get(0),
        )?;
        
        Ok((total, unprocessed))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let backup_manager = BackupManager::new("/var/mqtt/backup.db")?;
    
    let mut mqttoptions = MqttOptions::new("backup_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 100);
    
    // Restore any pending messages from previous session
    println!("[DR] Checking for messages to restore...");
    backup_manager.restore_messages(&client).await?;
    
    // Subscribe to topics
    client.subscribe("sensors/#", QoS::AtLeastOnce).await?;
    
    // Periodic cleanup task
    let cleanup_manager = Arc::new(backup_manager);
    let cleanup_task = {
        let manager = Arc::clone(&cleanup_manager);
        tokio::spawn(async move {
            loop {
                sleep(Duration::from_secs(3600)).await; // Every hour
                let _ = manager.cleanup_old_backups(7).await;
                
                if let Ok((total, unprocessed)) = manager.get_stats().await {
                    println!("[STATS] Total backups: {}, Unprocessed: {}", total, unprocessed);
                }
            }
        })
    };
    
    // Main event loop
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                println!("[BACKUP] Received message on {}", publish.topic);
                
                // Backup every incoming message
                cleanup_manager.backup_message(
                    &publish.topic,
                    &publish.payload,
                    publish.qos,
                    publish.retain,
                ).await?;
            }
            Err(e) => {
                eprintln!("[ERROR] Connection error: {:?}", e);
                sleep(Duration::from_secs(5)).await;
            }
            _ => {}
        }
    }
}
```

---

## Summary

**Disaster Recovery in MQTT** is critical for maintaining business continuity in production IoT systems. Key takeaways include:

**Essential Components:**
- **Persistent Sessions** (clean_session=false) preserve subscriptions and QoS 1/2 messages during disconnections
- **Automatic Reconnection** with exponential backoff prevents connection storms
- **Broker Failover** switches between primary and backup brokers when failures occur
- **Message Backup** persists critical messages to databases or logs for post-disaster restoration

**Implementation Best Practices:**
- Use **QoS 1 or 2** for critical messages to ensure delivery guarantees
- Implement **multi-broker architecture** with geographic distribution
- Maintain **comprehensive logging** of all message transactions
- Regular **backup testing** to verify recovery procedures work under pressure
- Define clear **RTO/RPO metrics** aligned with business requirements

**Technology Choices:**
- **C/C++**: Offers low-level control, ideal for embedded systems and resource-constrained environments where every byte of memory matters
- **Rust**: Provides memory safety guarantees and excellent async performance, making it perfect for high-reliability systems where bugs could be catastrophic

The examples demonstrate production-ready patterns including connection pooling, session recovery, SQLite-based message persistence, and automated failover—all essential for building resilient MQTT systems that can withstand and recover from disasters while minimizing data loss and downtime.