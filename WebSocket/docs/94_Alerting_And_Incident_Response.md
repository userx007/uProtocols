# WebSocket Alerting and Incident Response

## Overview

Alerting and incident response for WebSocket services involves implementing monitoring systems that detect anomalies, performance degradation, and failures in real-time, then triggering automated notifications and response workflows. This is critical for maintaining high availability and quickly resolving issues in production WebSocket applications.

## Key Concepts

### 1. **Monitoring Metrics**
- **Connection metrics**: Active connections, connection rate, disconnection rate
- **Performance metrics**: Message latency, throughput, processing time
- **Resource metrics**: CPU, memory, network bandwidth usage
- **Error metrics**: Connection failures, message delivery failures, protocol errors
- **Business metrics**: User engagement, feature usage, SLA compliance

### 2. **Alert Types**
- **Threshold alerts**: Triggered when metrics exceed predefined limits
- **Anomaly alerts**: Detect unusual patterns using baseline comparisons
- **Composite alerts**: Combine multiple conditions
- **Predictive alerts**: Use trending data to warn of future issues

### 3. **Incident Response Workflow**
- Detection → Classification → Notification → Investigation → Mitigation → Resolution → Post-mortem

---

## C/C++ Implementation

### Basic Metrics Collection and Alerting System

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

// Metrics structure
typedef struct {
    unsigned long active_connections;
    unsigned long total_connections;
    unsigned long failed_connections;
    unsigned long messages_sent;
    unsigned long messages_received;
    double avg_latency_ms;
    double cpu_usage;
    double memory_usage;
    pthread_mutex_t lock;
} WSMetrics;

// Alert configuration
typedef struct {
    char name[64];
    double threshold;
    bool enabled;
    time_t last_triggered;
    int cooldown_seconds;
} AlertRule;

// Alert severity levels
typedef enum {
    ALERT_INFO,
    ALERT_WARNING,
    ALERT_ERROR,
    ALERT_CRITICAL
} AlertSeverity;

// Global metrics
WSMetrics g_metrics = {0};

// Alert rules
AlertRule g_alert_rules[] = {
    {"high_connection_rate", 1000.0, true, 0, 60},
    {"high_latency", 500.0, true, 0, 120},
    {"high_error_rate", 0.05, true, 0, 300},
    {"memory_threshold", 80.0, true, 0, 180}
};

// Initialize metrics
void metrics_init(WSMetrics *metrics) {
    memset(metrics, 0, sizeof(WSMetrics));
    pthread_mutex_init(&metrics->lock, NULL);
}

// Update connection metrics
void metrics_update_connection(WSMetrics *metrics, bool success) {
    pthread_mutex_lock(&metrics->lock);
    metrics->total_connections++;
    if (success) {
        metrics->active_connections++;
    } else {
        metrics->failed_connections++;
    }
    pthread_mutex_unlock(&metrics->lock);
}

// Update message metrics
void metrics_update_message(WSMetrics *metrics, bool sent, double latency_ms) {
    pthread_mutex_lock(&metrics->lock);
    if (sent) {
        metrics->messages_sent++;
    } else {
        metrics->messages_received++;
    }
    
    // Update rolling average latency
    double alpha = 0.1; // Smoothing factor
    metrics->avg_latency_ms = alpha * latency_ms + (1 - alpha) * metrics->avg_latency_ms;
    pthread_mutex_unlock(&metrics->lock);
}

// Calculate error rate
double metrics_get_error_rate(WSMetrics *metrics) {
    pthread_mutex_lock(&metrics->lock);
    double rate = 0.0;
    if (metrics->total_connections > 0) {
        rate = (double)metrics->failed_connections / metrics->total_connections;
    }
    pthread_mutex_unlock(&metrics->lock);
    return rate;
}

// Alert notification function
void send_alert(AlertSeverity severity, const char *rule_name, 
                const char *message, double current_value) {
    const char *severity_str[] = {"INFO", "WARNING", "ERROR", "CRITICAL"};
    time_t now = time(NULL);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0'; // Remove newline
    
    printf("\n========== ALERT ==========\n");
    printf("Time: %s\n", timestamp);
    printf("Severity: %s\n", severity_str[severity]);
    printf("Rule: %s\n", rule_name);
    printf("Message: %s\n", message);
    printf("Current Value: %.2f\n", current_value);
    printf("===========================\n\n");
    
    // In production, integrate with:
    // - PagerDuty API
    // - Slack webhooks
    // - Email notifications
    // - SMS alerts
    // - Logging systems (syslog, etc.)
}

// Check if alert should be triggered (respects cooldown)
bool should_trigger_alert(AlertRule *rule) {
    time_t now = time(NULL);
    if (difftime(now, rule->last_triggered) >= rule->cooldown_seconds) {
        rule->last_triggered = now;
        return true;
    }
    return false;
}

// Evaluate alert rules
void evaluate_alerts(WSMetrics *metrics) {
    // Check connection rate
    if (g_alert_rules[0].enabled) {
        pthread_mutex_lock(&metrics->lock);
        unsigned long conn_count = metrics->active_connections;
        pthread_mutex_unlock(&metrics->lock);
        
        if (conn_count > g_alert_rules[0].threshold && 
            should_trigger_alert(&g_alert_rules[0])) {
            send_alert(ALERT_WARNING, g_alert_rules[0].name,
                      "Active connection count exceeds threshold",
                      (double)conn_count);
        }
    }
    
    // Check latency
    if (g_alert_rules[1].enabled) {
        pthread_mutex_lock(&metrics->lock);
        double latency = metrics->avg_latency_ms;
        pthread_mutex_unlock(&metrics->lock);
        
        if (latency > g_alert_rules[1].threshold && 
            should_trigger_alert(&g_alert_rules[1])) {
            send_alert(ALERT_ERROR, g_alert_rules[1].name,
                      "Average latency exceeds threshold",
                      latency);
        }
    }
    
    // Check error rate
    if (g_alert_rules[2].enabled) {
        double error_rate = metrics_get_error_rate(metrics);
        
        if (error_rate > g_alert_rules[2].threshold && 
            should_trigger_alert(&g_alert_rules[2])) {
            send_alert(ALERT_CRITICAL, g_alert_rules[2].name,
                      "Error rate exceeds threshold",
                      error_rate * 100);
        }
    }
}

// Monitoring thread
void* monitoring_thread(void *arg) {
    WSMetrics *metrics = (WSMetrics*)arg;
    
    while (1) {
        sleep(10); // Check every 10 seconds
        evaluate_alerts(metrics);
        
        // Print current metrics
        pthread_mutex_lock(&metrics->lock);
        printf("Metrics: Connections=%lu, Messages Sent=%lu, "
               "Avg Latency=%.2fms, Error Rate=%.2f%%\n",
               metrics->active_connections,
               metrics->messages_sent,
               metrics->avg_latency_ms,
               metrics_get_error_rate(metrics) * 100);
        pthread_mutex_unlock(&metrics->lock);
    }
    
    return NULL;
}

// Health check endpoint data
typedef struct {
    bool is_healthy;
    char status[32];
    time_t last_check;
    WSMetrics *metrics;
} HealthCheck;

// Perform health check
void perform_health_check(HealthCheck *health, WSMetrics *metrics) {
    health->last_check = time(NULL);
    health->metrics = metrics;
    
    // Check various health indicators
    double error_rate = metrics_get_error_rate(metrics);
    bool high_error = error_rate > 0.1;
    bool high_latency = metrics->avg_latency_ms > 1000.0;
    
    if (high_error || high_latency) {
        health->is_healthy = false;
        strcpy(health->status, "DEGRADED");
    } else {
        health->is_healthy = true;
        strcpy(health->status, "HEALTHY");
    }
}

int main() {
    metrics_init(&g_metrics);
    
    // Start monitoring thread
    pthread_t monitor_tid;
    pthread_create(&monitor_tid, NULL, monitoring_thread, &g_metrics);
    
    // Simulate WebSocket activity
    for (int i = 0; i < 1500; i++) {
        // Simulate successful connection
        metrics_update_connection(&g_metrics, true);
        
        // Simulate message with varying latency
        double latency = 100.0 + (rand() % 400);
        metrics_update_message(&g_metrics, true, latency);
        
        // Simulate occasional failures
        if (rand() % 20 == 0) {
            metrics_update_connection(&g_metrics, false);
        }
        
        usleep(10000); // 10ms delay
    }
    
    sleep(5);
    
    // Cleanup
    pthread_mutex_destroy(&g_metrics.lock);
    return 0;
}
```

### Advanced Incident Response System

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// Incident structure
typedef struct {
    int id;
    time_t created_at;
    time_t resolved_at;
    char title[128];
    char description[512];
    AlertSeverity severity;
    bool is_resolved;
    char assigned_to[64];
} Incident;

// Incident database (simple array for demo)
#define MAX_INCIDENTS 100
Incident g_incidents[MAX_INCIDENTS];
int g_incident_count = 0;

// Create new incident
int create_incident(const char *title, const char *description, 
                    AlertSeverity severity) {
    if (g_incident_count >= MAX_INCIDENTS) {
        return -1;
    }
    
    Incident *incident = &g_incidents[g_incident_count];
    incident->id = g_incident_count + 1;
    incident->created_at = time(NULL);
    incident->resolved_at = 0;
    strncpy(incident->title, title, sizeof(incident->title) - 1);
    strncpy(incident->description, description, sizeof(incident->description) - 1);
    incident->severity = severity;
    incident->is_resolved = false;
    strcpy(incident->assigned_to, "on-call-engineer");
    
    printf("INCIDENT CREATED: #%d - %s\n", incident->id, incident->title);
    
    g_incident_count++;
    return incident->id;
}

// Resolve incident
void resolve_incident(int incident_id, const char *resolution_notes) {
    for (int i = 0; i < g_incident_count; i++) {
        if (g_incidents[i].id == incident_id) {
            g_incidents[i].is_resolved = true;
            g_incidents[i].resolved_at = time(NULL);
            printf("INCIDENT RESOLVED: #%d - %s\n", 
                   incident_id, resolution_notes);
            return;
        }
    }
}

// Automated remediation actions
void auto_remediate(const char *issue_type) {
    printf("AUTOMATED REMEDIATION: %s\n", issue_type);
    
    if (strcmp(issue_type, "high_memory") == 0) {
        printf("  -> Triggering garbage collection\n");
        printf("  -> Closing idle connections\n");
    } else if (strcmp(issue_type, "connection_storm") == 0) {
        printf("  -> Enabling rate limiting\n");
        printf("  -> Scaling up instances\n");
    } else if (strcmp(issue_type, "high_latency") == 0) {
        printf("  -> Switching to backup message queue\n");
        printf("  -> Clearing cache\n");
    }
}
```

---

## Rust Implementation

### Comprehensive Alerting and Monitoring System

```rust
use std::collections::HashMap;
use std::sync::{Arc, RwLock};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::sync::mpsc;
use tokio::time::interval;
use serde::{Deserialize, Serialize};

// Metrics structure
#[derive(Debug, Clone, Default)]
pub struct WebSocketMetrics {
    pub active_connections: u64,
    pub total_connections: u64,
    pub failed_connections: u64,
    pub messages_sent: u64,
    pub messages_received: u64,
    pub avg_latency_ms: f64,
    pub cpu_usage: f64,
    pub memory_usage_mb: f64,
    pub last_updated: Instant,
}

impl WebSocketMetrics {
    pub fn error_rate(&self) -> f64 {
        if self.total_connections == 0 {
            0.0
        } else {
            self.failed_connections as f64 / self.total_connections as f64
        }
    }
    
    pub fn update_latency(&mut self, new_latency: f64) {
        // Exponential moving average
        let alpha = 0.1;
        self.avg_latency_ms = alpha * new_latency + (1.0 - alpha) * self.avg_latency_ms;
    }
}

// Alert severity
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum AlertSeverity {
    Info,
    Warning,
    Error,
    Critical,
}

// Alert rule
#[derive(Debug, Clone)]
pub struct AlertRule {
    pub name: String,
    pub description: String,
    pub threshold: f64,
    pub enabled: bool,
    pub cooldown: Duration,
    pub last_triggered: Option<Instant>,
    pub evaluator: fn(&WebSocketMetrics) -> Option<f64>,
}

impl AlertRule {
    pub fn can_trigger(&self) -> bool {
        if !self.enabled {
            return false;
        }
        
        if let Some(last) = self.last_triggered {
            last.elapsed() >= self.cooldown
        } else {
            true
        }
    }
    
    pub fn evaluate(&mut self, metrics: &WebSocketMetrics) -> Option<Alert> {
        if !self.can_trigger() {
            return None;
        }
        
        if let Some(current_value) = (self.evaluator)(metrics) {
            if current_value > self.threshold {
                self.last_triggered = Some(Instant::now());
                
                let severity = if current_value > self.threshold * 2.0 {
                    AlertSeverity::Critical
                } else if current_value > self.threshold * 1.5 {
                    AlertSeverity::Error
                } else {
                    AlertSeverity::Warning
                };
                
                return Some(Alert {
                    rule_name: self.name.clone(),
                    severity,
                    message: format!(
                        "{}: current value {:.2} exceeds threshold {:.2}",
                        self.description, current_value, self.threshold
                    ),
                    current_value,
                    timestamp: SystemTime::now(),
                });
            }
        }
        
        None
    }
}

// Alert
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Alert {
    pub rule_name: String,
    pub severity: AlertSeverity,
    pub message: String,
    pub current_value: f64,
    pub timestamp: SystemTime,
}

// Incident
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Incident {
    pub id: u64,
    pub title: String,
    pub description: String,
    pub severity: AlertSeverity,
    pub created_at: SystemTime,
    pub resolved_at: Option<SystemTime>,
    pub assigned_to: String,
    pub alerts: Vec<Alert>,
}

// Alerting system
pub struct AlertingSystem {
    metrics: Arc<RwLock<WebSocketMetrics>>,
    rules: Vec<AlertRule>,
    incidents: Arc<RwLock<HashMap<u64, Incident>>>,
    next_incident_id: u64,
    alert_sender: mpsc::Sender<Alert>,
}

impl AlertingSystem {
    pub fn new(metrics: Arc<RwLock<WebSocketMetrics>>) -> (Self, mpsc::Receiver<Alert>) {
        let (alert_sender, alert_receiver) = mpsc::channel(100);
        
        let rules = vec![
            AlertRule {
                name: "high_connection_count".to_string(),
                description: "Active connection count is high".to_string(),
                threshold: 1000.0,
                enabled: true,
                cooldown: Duration::from_secs(60),
                last_triggered: None,
                evaluator: |m| Some(m.active_connections as f64),
            },
            AlertRule {
                name: "high_latency".to_string(),
                description: "Average message latency is high".to_string(),
                threshold: 500.0,
                enabled: true,
                cooldown: Duration::from_secs(120),
                last_triggered: None,
                evaluator: |m| Some(m.avg_latency_ms),
            },
            AlertRule {
                name: "high_error_rate".to_string(),
                description: "Connection error rate is high".to_string(),
                threshold: 0.05,
                enabled: true,
                cooldown: Duration::from_secs(300),
                last_triggered: None,
                evaluator: |m| Some(m.error_rate()),
            },
            AlertRule {
                name: "memory_threshold".to_string(),
                description: "Memory usage exceeds threshold".to_string(),
                threshold: 1024.0, // 1GB
                enabled: true,
                cooldown: Duration::from_secs(180),
                last_triggered: None,
                evaluator: |m| Some(m.memory_usage_mb),
            },
        ];
        
        (
            Self {
                metrics,
                rules,
                incidents: Arc::new(RwLock::new(HashMap::new())),
                next_incident_id: 1,
                alert_sender,
            },
            alert_receiver,
        )
    }
    
    pub async fn start_monitoring(&mut self) {
        let mut interval = interval(Duration::from_secs(10));
        
        loop {
            interval.tick().await;
            self.evaluate_all_rules().await;
        }
    }
    
    async fn evaluate_all_rules(&mut self) {
        let metrics = self.metrics.read().unwrap().clone();
        
        for rule in &mut self.rules {
            if let Some(alert) = rule.evaluate(&metrics) {
                self.handle_alert(alert).await;
            }
        }
        
        // Print current metrics
        println!(
            "Metrics: Connections={}, Messages Sent={}, Avg Latency={:.2}ms, Error Rate={:.2}%",
            metrics.active_connections,
            metrics.messages_sent,
            metrics.avg_latency_ms,
            metrics.error_rate() * 100.0
        );
    }
    
    async fn handle_alert(&mut self, alert: Alert) {
        println!("\n========== ALERT ==========");
        println!("Time: {:?}", alert.timestamp);
        println!("Severity: {:?}", alert.severity);
        println!("Rule: {}", alert.rule_name);
        println!("Message: {}", alert.message);
        println!("Current Value: {:.2}", alert.current_value);
        println!("===========================\n");
        
        // Send alert through channel
        let _ = self.alert_sender.send(alert.clone()).await;
        
        // Create incident for critical alerts
        if alert.severity == AlertSeverity::Critical {
            self.create_incident(
                format!("Critical: {}", alert.rule_name),
                alert.message.clone(),
                alert.severity,
                vec![alert],
            );
        }
        
        // Trigger automated remediation
        self.auto_remediate(&alert.rule_name).await;
    }
    
    fn create_incident(
        &mut self,
        title: String,
        description: String,
        severity: AlertSeverity,
        alerts: Vec<Alert>,
    ) -> u64 {
        let incident_id = self.next_incident_id;
        self.next_incident_id += 1;
        
        let incident = Incident {
            id: incident_id,
            title: title.clone(),
            description,
            severity,
            created_at: SystemTime::now(),
            resolved_at: None,
            assigned_to: "on-call-engineer".to_string(),
            alerts,
        };
        
        println!("INCIDENT CREATED: #{} - {}", incident_id, title);
        
        self.incidents.write().unwrap().insert(incident_id, incident);
        incident_id
    }
    
    pub fn resolve_incident(&self, incident_id: u64, resolution_notes: String) {
        let mut incidents = self.incidents.write().unwrap();
        if let Some(incident) = incidents.get_mut(&incident_id) {
            incident.resolved_at = Some(SystemTime::now());
            println!("INCIDENT RESOLVED: #{} - {}", incident_id, resolution_notes);
        }
    }
    
    async fn auto_remediate(&self, issue_type: &str) {
        println!("AUTOMATED REMEDIATION: {}", issue_type);
        
        match issue_type {
            "high_connection_count" => {
                println!("  -> Enabling rate limiting");
                println!("  -> Scaling up instances");
            }
            "high_latency" => {
                println!("  -> Switching to backup message queue");
                println!("  -> Clearing cache");
            }
            "high_error_rate" => {
                println!("  -> Investigating error logs");
                println!("  -> Restarting unhealthy instances");
            }
            "memory_threshold" => {
                println!("  -> Triggering garbage collection");
                println!("  -> Closing idle connections");
            }
            _ => {}
        }
    }
    
    pub fn get_active_incidents(&self) -> Vec<Incident> {
        self.incidents
            .read()
            .unwrap()
            .values()
            .filter(|i| i.resolved_at.is_none())
            .cloned()
            .collect()
    }
}

// Alert notification handler
pub struct AlertNotifier {
    alert_receiver: mpsc::Receiver<Alert>,
}

impl AlertNotifier {
    pub fn new(alert_receiver: mpsc::Receiver<Alert>) -> Self {
        Self { alert_receiver }
    }
    
    pub async fn start(&mut self) {
        while let Some(alert) = self.alert_receiver.recv().await {
            self.send_notifications(&alert).await;
        }
    }
    
    async fn send_notifications(&self, alert: &Alert) {
        // Send to various channels based on severity
        match alert.severity {
            AlertSeverity::Critical => {
                self.send_pagerduty(alert).await;
                self.send_slack(alert).await;
                self.send_email(alert).await;
            }
            AlertSeverity::Error => {
                self.send_slack(alert).await;
                self.send_email(alert).await;
            }
            AlertSeverity::Warning => {
                self.send_slack(alert).await;
            }
            AlertSeverity::Info => {
                // Log only
            }
        }
    }
    
    async fn send_pagerduty(&self, alert: &Alert) {
        println!("📟 PagerDuty: {:?} - {}", alert.severity, alert.message);
        // Integrate with PagerDuty API
    }
    
    async fn send_slack(&self, alert: &Alert) {
        println!("💬 Slack: {:?} - {}", alert.severity, alert.message);
        // Integrate with Slack webhook
    }
    
    async fn send_email(&self, alert: &Alert) {
        println!("📧 Email: {:?} - {}", alert.severity, alert.message);
        // Send email notification
    }
}

// Health check
#[derive(Debug, Serialize, Deserialize)]
pub struct HealthStatus {
    pub status: String,
    pub healthy: bool,
    pub metrics: WebSocketMetrics,
    pub timestamp: SystemTime,
}

pub fn perform_health_check(metrics: &WebSocketMetrics) -> HealthStatus {
    let error_rate = metrics.error_rate();
    let healthy = error_rate < 0.1 && metrics.avg_latency_ms < 1000.0;
    
    HealthStatus {
        status: if healthy { "healthy".to_string() } else { "degraded".to_string() },
        healthy,
        metrics: metrics.clone(),
        timestamp: SystemTime::now(),
    }
}

// Example usage
#[tokio::main]
async fn main() {
    let metrics = Arc::new(RwLock::new(WebSocketMetrics::default()));
    let (mut alerting_system, alert_receiver) = AlertingSystem::new(Arc::clone(&metrics));
    
    // Start alert notifier
    let mut notifier = AlertNotifier::new(alert_receiver);
    tokio::spawn(async move {
        notifier.start().await;
    });
    
    // Start monitoring
    let monitoring_handle = {
        let mut system = alerting_system;
        tokio::spawn(async move {
            system.start_monitoring().await;
        })
    };
    
    // Simulate WebSocket activity
    for i in 0..2000 {
        let mut m = metrics.write().unwrap();
        m.active_connections += 1;
        m.total_connections += 1;
        m.messages_sent += 1;
        
        // Simulate varying latency
        let latency = 100.0 + (i % 500) as f64;
        m.update_latency(latency);
        
        // Simulate occasional failures
        if i % 20 == 0 {
            m.failed_connections += 1;
        }
        
        drop(m);
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    
    // Wait a bit for alerts to process
    tokio::time::sleep(Duration::from_secs(5)).await;
    
    monitoring_handle.abort();
}
```

---

## Summary

**WebSocket Alerting and Incident Response** is essential for maintaining reliable real-time communication services. Key takeaways:

### Core Components:
1. **Metrics Collection**: Track connections, latency, errors, and resource usage
2. **Alert Rules**: Define thresholds and conditions for notifications
3. **Incident Management**: Create, track, and resolve incidents systematically
4. **Automated Remediation**: Implement self-healing capabilities for common issues
5. **Multi-channel Notifications**: Alert via PagerDuty, Slack, email based on severity

### Best Practices:
- **Use exponential moving averages** for smoothing noisy metrics
- **Implement alert cooldowns** to prevent notification storms
- **Scale alerts by severity**: Info → Warning → Error → Critical
- **Combine automated remediation** with human oversight
- **Maintain comprehensive incident history** for post-mortems
- **Monitor both technical and business metrics**
- **Implement health check endpoints** for load balancers
- **Use structured logging** for correlation with alerts

### Implementation Considerations:
- **C/C++**: Best for low-level system integration and performance-critical monitoring
- **Rust**: Provides safety, concurrency, and excellent async support for complex alerting workflows
- **Alert fatigue**: Tune thresholds carefully to avoid over-alerting
- **False positives**: Use composite conditions and baselines to reduce noise
- **Response time**: Critical alerts should page on-call engineers immediately

This comprehensive monitoring and alerting infrastructure ensures WebSocket services maintain high availability and rapid incident resolution.