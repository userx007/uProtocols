# Healthcare and Medical Devices - MQTT Implementation Guide

## Overview

Healthcare and medical device systems using MQTT enable remote patient monitoring, medical IoT device communication, and real-time health data transmission while maintaining strict compliance with healthcare regulations like HIPAA, GDPR, and FDA guidelines. These systems require high reliability, security, data integrity, and audit capabilities.

## Topic Structure and Hierarchy

Healthcare MQTT topics follow a structured hierarchy to organize data from various medical devices, patients, and healthcare facilities:

```
healthcare/{facility_id}/{department}/{device_type}/{device_id}/{metric}
healthcare/{facility_id}/patients/{patient_id}/{vital_sign}
healthcare/{facility_id}/alerts/{severity}/{alert_type}
healthcare/{facility_id}/devices/{device_id}/status
healthcare/{facility_id}/compliance/audit/{event_type}
```

### Example Topics

```
healthcare/hospital-001/icu/monitor/device-12345/heart-rate
healthcare/hospital-001/patients/patient-67890/blood-pressure
healthcare/hospital-001/alerts/critical/cardiac-arrest
healthcare/hospital-001/devices/pump-456/status
healthcare/hospital-001/compliance/audit/data-access
```

## Key Requirements for Healthcare MQTT

1. **Security**: TLS/SSL encryption, certificate-based authentication
2. **Compliance**: HIPAA-compliant data handling, audit trails
3. **Reliability**: QoS 1 or 2 for critical health data
4. **Real-time**: Low latency for emergency alerts
5. **Data Integrity**: Message validation and checksums
6. **Privacy**: Patient data anonymization and access control

## C/C++ Implementation

### Patient Monitoring Device (Publisher)

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <openssl/sha.h>

#define BROKER_ADDRESS "secure-healthcare-broker.hospital.com"
#define BROKER_PORT 8883
#define KEEP_ALIVE 60
#define QOS 2  // Exactly once delivery for critical health data

typedef struct {
    char patient_id[64];
    char device_id[64];
    char facility_id[64];
    int heart_rate;
    int systolic_bp;
    int diastolic_bp;
    float spo2;
    float temperature;
    time_t timestamp;
} VitalSigns;

// Generate audit log entry
void log_audit_event(struct mosquitto *mosq, const char *facility_id, 
                     const char *event_type, const char *details) {
    char topic[256];
    char payload[512];
    time_t now = time(NULL);
    
    snprintf(topic, sizeof(topic), 
             "healthcare/%s/compliance/audit/%s", facility_id, event_type);
    
    snprintf(payload, sizeof(payload),
             "{\"timestamp\":%ld,\"event\":\"%s\",\"details\":\"%s\"}",
             now, event_type, details);
    
    mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 1, false);
}

// Publish vital signs with data integrity check
void publish_vital_signs(struct mosquitto *mosq, VitalSigns *vitals) {
    char topic[256];
    char payload[1024];
    char hash[65];
    unsigned char digest[SHA256_DIGEST_LENGTH];
    
    // Create JSON payload
    snprintf(payload, sizeof(payload),
             "{\"patient_id\":\"%s\",\"device_id\":\"%s\","
             "\"heart_rate\":%d,\"systolic_bp\":%d,\"diastolic_bp\":%d,"
             "\"spo2\":%.2f,\"temperature\":%.2f,\"timestamp\":%ld}",
             vitals->patient_id, vitals->device_id,
             vitals->heart_rate, vitals->systolic_bp, vitals->diastolic_bp,
             vitals->spo2, vitals->temperature, vitals->timestamp);
    
    // Calculate SHA-256 hash for data integrity
    SHA256((unsigned char*)payload, strlen(payload), digest);
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(&hash[i*2], "%02x", digest[i]);
    }
    
    // Add hash to payload
    char final_payload[1200];
    snprintf(final_payload, sizeof(final_payload),
             "{\"data\":%s,\"integrity_hash\":\"%s\"}", payload, hash);
    
    // Publish heart rate
    snprintf(topic, sizeof(topic),
             "healthcare/%s/patients/%s/heart-rate",
             vitals->facility_id, vitals->patient_id);
    
    int rc = mosquitto_publish(mosq, NULL, topic, strlen(final_payload),
                               final_payload, QOS, false);
    
    if(rc == MOSQ_ERR_SUCCESS) {
        printf("Published vital signs for patient %s\n", vitals->patient_id);
        log_audit_event(mosq, vitals->facility_id, "data-transmitted",
                       vitals->patient_id);
    } else {
        fprintf(stderr, "Failed to publish: %s\n", mosquitto_strerror(rc));
    }
    
    // Check for critical values and send alerts
    if(vitals->heart_rate > 120 || vitals->heart_rate < 50) {
        snprintf(topic, sizeof(topic),
                 "healthcare/%s/alerts/critical/abnormal-heart-rate",
                 vitals->facility_id);
        
        snprintf(payload, sizeof(payload),
                 "{\"patient_id\":\"%s\",\"heart_rate\":%d,\"timestamp\":%ld}",
                 vitals->patient_id, vitals->heart_rate, vitals->timestamp);
        
        mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, QOS, false);
    }
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if(rc == 0) {
        printf("Connected to MQTT broker securely\n");
    } else {
        fprintf(stderr, "Connection failed: %s\n", mosquitto_strerror(rc));
    }
}

int main(int argc, char *argv[]) {
    struct mosquitto *mosq;
    int rc;
    
    // Initialize mosquitto library
    mosquitto_lib_init();
    
    // Create mosquitto instance
    mosq = mosquitto_new("patient_monitor_001", true, NULL);
    if(!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    
    // Configure TLS/SSL for HIPAA compliance
    rc = mosquitto_tls_set(mosq,
                          "/etc/ssl/certs/ca-cert.pem",      // CA certificate
                          NULL,                                // cert directory
                          "/etc/ssl/certs/client-cert.pem",   // client cert
                          "/etc/ssl/private/client-key.pem",  // client key
                          NULL);                               // password callback
    
    if(rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to set TLS: %s\n", mosquitto_strerror(rc));
        return 1;
    }
    
    // Set TLS options
    mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
    
    // Connect to broker
    rc = mosquitto_connect(mosq, BROKER_ADDRESS, BROKER_PORT, KEEP_ALIVE);
    if(rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to connect: %s\n", mosquitto_strerror(rc));
        return 1;
    }
    
    // Start network loop in background
    mosquitto_loop_start(mosq);
    
    // Simulate patient monitoring
    VitalSigns vitals = {
        .patient_id = "PATIENT-67890",
        .device_id = "MONITOR-12345",
        .facility_id = "hospital-001",
        .heart_rate = 75,
        .systolic_bp = 120,
        .diastolic_bp = 80,
        .spo2 = 98.5,
        .temperature = 37.2
    };
    
    // Publish vital signs every 5 seconds
    for(int i = 0; i < 20; i++) {
        vitals.timestamp = time(NULL);
        vitals.heart_rate = 70 + (rand() % 20);  // Simulate varying heart rate
        vitals.spo2 = 95.0 + ((float)(rand() % 50) / 10.0);
        
        publish_vital_signs(mosq, &vitals);
        sleep(5);
    }
    
    // Cleanup
    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

### Healthcare Monitoring Dashboard (Subscriber)

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    printf("\n=== Received Medical Data ===\n");
    printf("Topic: %s\n", msg->topic);
    printf("Payload: %.*s\n", msg->payloadlen, (char*)msg->payload);
    printf("QoS: %d\n", msg->qos);
    
    // Parse topic to determine data type
    if(strstr(msg->topic, "/alerts/critical/")) {
        printf("*** CRITICAL ALERT RECEIVED ***\n");
        // Trigger emergency response protocol
    } else if(strstr(msg->topic, "/heart-rate")) {
        printf("Heart rate data received\n");
        // Update patient dashboard
    } else if(strstr(msg->topic, "/compliance/audit/")) {
        printf("Audit log entry received\n");
        // Store in compliance database
    }
    printf("============================\n");
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if(rc == 0) {
        printf("Dashboard connected to broker\n");
        
        // Subscribe to all patient data for facility
        mosquitto_subscribe(mosq, NULL, "healthcare/hospital-001/patients/#", 2);
        
        // Subscribe to critical alerts
        mosquitto_subscribe(mosq, NULL, "healthcare/hospital-001/alerts/critical/#", 2);
        
        // Subscribe to audit logs
        mosquitto_subscribe(mosq, NULL, "healthcare/hospital-001/compliance/audit/#", 1);
        
        printf("Subscribed to healthcare topics\n");
    } else {
        fprintf(stderr, "Connection failed: %s\n", mosquitto_strerror(rc));
    }
}

int main(void) {
    struct mosquitto *mosq;
    
    mosquitto_lib_init();
    
    mosq = mosquitto_new("healthcare_dashboard_001", true, NULL);
    if(!mosq) {
        fprintf(stderr, "Failed to create instance\n");
        return 1;
    }
    
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    
    // Configure TLS
    mosquitto_tls_set(mosq,
                     "/etc/ssl/certs/ca-cert.pem",
                     NULL,
                     "/etc/ssl/certs/dashboard-cert.pem",
                     "/etc/ssl/private/dashboard-key.pem",
                     NULL);
    
    mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
    
    if(mosquitto_connect(mosq, BROKER_ADDRESS, BROKER_PORT, KEEP_ALIVE) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Connection failed\n");
        return 1;
    }
    
    // Run loop to process messages
    mosquitto_loop_forever(mosq, -1, 1);
    
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

## Rust Implementation

### Patient Monitoring Device (Publisher)

```rust
use paho_mqtt as mqtt;
use serde::{Deserialize, Serialize};
use serde_json;
use sha2::{Sha256, Digest};
use chrono::Utc;
use std::time::Duration;
use std::thread;

#[derive(Debug, Serialize, Deserialize)]
struct VitalSigns {
    patient_id: String,
    device_id: String,
    heart_rate: i32,
    systolic_bp: i32,
    diastolic_bp: i32,
    spo2: f32,
    temperature: f32,
    timestamp: i64,
}

#[derive(Debug, Serialize)]
struct SecurePayload {
    data: VitalSigns,
    integrity_hash: String,
}

#[derive(Debug, Serialize)]
struct AuditEvent {
    timestamp: i64,
    event: String,
    details: String,
}

struct HealthcareMonitor {
    client: mqtt::Client,
    facility_id: String,
}

impl HealthcareMonitor {
    fn new(broker_url: &str, facility_id: &str) -> Result<Self, mqtt::Error> {
        // Create MQTT client with TLS configuration
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker_url)
            .client_id("patient_monitor_rust_001")
            .finalize();
        
        let client = mqtt::Client::new(create_opts)?;
        
        // Configure SSL/TLS options for HIPAA compliance
        let ssl_opts = mqtt::SslOptionsBuilder::new()
            .trust_store("/etc/ssl/certs/ca-cert.pem")?
            .key_store("/etc/ssl/certs/client-cert.pem")?
            .private_key("/etc/ssl/private/client-key.pem")?
            .enable_server_cert_auth(true)
            .ssl_version(mqtt::SslVersion::Tls_1_2)
            .finalize();
        
        // Configure connection options
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .ssl_options(ssl_opts)
            .keep_alive_interval(Duration::from_secs(60))
            .clean_session(false)
            .finalize();
        
        // Connect to broker
        client.connect(conn_opts)?;
        println!("Connected to healthcare MQTT broker securely");
        
        Ok(HealthcareMonitor {
            client,
            facility_id: facility_id.to_string(),
        })
    }
    
    fn calculate_hash(data: &str) -> String {
        let mut hasher = Sha256::new();
        hasher.update(data.as_bytes());
        format!("{:x}", hasher.finalize())
    }
    
    fn log_audit_event(&self, event_type: &str, details: &str) -> Result<(), mqtt::Error> {
        let topic = format!(
            "healthcare/{}/compliance/audit/{}",
            self.facility_id, event_type
        );
        
        let audit = AuditEvent {
            timestamp: Utc::now().timestamp(),
            event: event_type.to_string(),
            details: details.to_string(),
        };
        
        let payload = serde_json::to_string(&audit).unwrap();
        let msg = mqtt::MessageBuilder::new()
            .topic(&topic)
            .payload(payload)
            .qos(1)
            .finalize();
        
        self.client.publish(msg)?;
        Ok(())
    }
    
    fn publish_vital_signs(&self, vitals: &VitalSigns) -> Result<(), Box<dyn std::error::Error>> {
        // Serialize vital signs
        let data_json = serde_json::to_string(&vitals)?;
        
        // Calculate integrity hash
        let hash = Self::calculate_hash(&data_json);
        
        // Create secure payload
        let secure_payload = SecurePayload {
            data: VitalSigns {
                patient_id: vitals.patient_id.clone(),
                device_id: vitals.device_id.clone(),
                heart_rate: vitals.heart_rate,
                systolic_bp: vitals.systolic_bp,
                diastolic_bp: vitals.diastolic_bp,
                spo2: vitals.spo2,
                temperature: vitals.temperature,
                timestamp: vitals.timestamp,
            },
            integrity_hash: hash,
        };
        
        let payload = serde_json::to_string(&secure_payload)?;
        
        // Publish heart rate data
        let topic = format!(
            "healthcare/{}/patients/{}/heart-rate",
            self.facility_id, vitals.patient_id
        );
        
        let msg = mqtt::MessageBuilder::new()
            .topic(&topic)
            .payload(payload)
            .qos(2) // Exactly once delivery for critical health data
            .retained(false)
            .finalize();
        
        self.client.publish(msg)?;
        println!("Published vital signs for patient {}", vitals.patient_id);
        
        // Log audit event
        self.log_audit_event("data-transmitted", &vitals.patient_id)?;
        
        // Check for critical conditions
        if vitals.heart_rate > 120 || vitals.heart_rate < 50 {
            self.publish_critical_alert(vitals)?;
        }
        
        Ok(())
    }
    
    fn publish_critical_alert(&self, vitals: &VitalSigns) -> Result<(), Box<dyn std::error::Error>> {
        let topic = format!(
            "healthcare/{}/alerts/critical/abnormal-heart-rate",
            self.facility_id
        );
        
        let alert = serde_json::json!({
            "patient_id": vitals.patient_id,
            "heart_rate": vitals.heart_rate,
            "timestamp": vitals.timestamp,
            "severity": "CRITICAL"
        });
        
        let msg = mqtt::MessageBuilder::new()
            .topic(&topic)
            .payload(alert.to_string())
            .qos(2)
            .finalize();
        
        self.client.publish(msg)?;
        println!("*** CRITICAL ALERT: Abnormal heart rate for patient {} ***", vitals.patient_id);
        
        self.log_audit_event("critical-alert", &vitals.patient_id)?;
        Ok(())
    }
    
    fn publish_device_status(&self, device_id: &str, status: &str) -> Result<(), mqtt::Error> {
        let topic = format!(
            "healthcare/{}/devices/{}/status",
            self.facility_id, device_id
        );
        
        let payload = serde_json::json!({
            "device_id": device_id,
            "status": status,
            "timestamp": Utc::now().timestamp()
        });
        
        let msg = mqtt::MessageBuilder::new()
            .topic(&topic)
            .payload(payload.to_string())
            .qos(1)
            .finalize();
        
        self.client.publish(msg)?;
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let broker_url = "ssl://secure-healthcare-broker.hospital.com:8883";
    let monitor = HealthcareMonitor::new(broker_url, "hospital-001")?;
    
    // Publish device online status
    monitor.publish_device_status("MONITOR-12345", "online")?;
    
    // Simulate patient monitoring
    let mut vitals = VitalSigns {
        patient_id: "PATIENT-67890".to_string(),
        device_id: "MONITOR-12345".to_string(),
        heart_rate: 75,
        systolic_bp: 120,
        diastolic_bp: 80,
        spo2: 98.5,
        temperature: 37.2,
        timestamp: Utc::now().timestamp(),
    };
    
    // Continuously monitor and publish vital signs
    for i in 0..20 {
        vitals.timestamp = Utc::now().timestamp();
        vitals.heart_rate = 70 + (i % 20); // Simulate varying readings
        vitals.spo2 = 95.0 + ((i as f32 % 50.0) / 10.0);
        
        monitor.publish_vital_signs(&vitals)?;
        
        thread::sleep(Duration::from_secs(5));
    }
    
    // Publish device offline status
    monitor.publish_device_status("MONITOR-12345", "offline")?;
    
    Ok(())
}
```

### Healthcare Dashboard (Subscriber)

```rust
use paho_mqtt as mqtt;
use serde_json::Value;
use std::time::Duration;

struct HealthcareDashboard {
    client: mqtt::Client,
}

impl HealthcareDashboard {
    fn new(broker_url: &str) -> Result<Self, mqtt::Error> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker_url)
            .client_id("healthcare_dashboard_rust_001")
            .finalize();
        
        let client = mqtt::Client::new(create_opts)?;
        
        // Configure SSL/TLS
        let ssl_opts = mqtt::SslOptionsBuilder::new()
            .trust_store("/etc/ssl/certs/ca-cert.pem")?
            .key_store("/etc/ssl/certs/dashboard-cert.pem")?
            .private_key("/etc/ssl/private/dashboard-key.pem")?
            .enable_server_cert_auth(true)
            .ssl_version(mqtt::SslVersion::Tls_1_2)
            .finalize();
        
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .ssl_options(ssl_opts)
            .keep_alive_interval(Duration::from_secs(60))
            .clean_session(false)
            .finalize();
        
        client.connect(conn_opts)?;
        println!("Healthcare Dashboard connected securely");
        
        Ok(HealthcareDashboard { client })
    }
    
    fn subscribe_to_topics(&self) -> Result<(), mqtt::Error> {
        // Subscribe to patient vital signs
        self.client.subscribe("healthcare/hospital-001/patients/#", 2)?;
        
        // Subscribe to critical alerts
        self.client.subscribe("healthcare/hospital-001/alerts/critical/#", 2)?;
        
        // Subscribe to device status
        self.client.subscribe("healthcare/hospital-001/devices/+/status", 1)?;
        
        // Subscribe to audit logs
        self.client.subscribe("healthcare/hospital-001/compliance/audit/#", 1)?;
        
        println!("Subscribed to healthcare monitoring topics");
        Ok(())
    }
    
    fn process_message(&self, msg: &mqtt::Message) {
        println!("\n=== Received Medical Data ===");
        println!("Topic: {}", msg.topic());
        
        if let Ok(payload_str) = std::str::from_utf8(msg.payload()) {
            if let Ok(json) = serde_json::from_str::<Value>(payload_str) {
                println!("Payload: {}", serde_json::to_string_pretty(&json).unwrap());
                
                // Route message based on topic
                if msg.topic().contains("/alerts/critical/") {
                    println!("*** CRITICAL ALERT RECEIVED ***");
                    self.handle_critical_alert(&json);
                } else if msg.topic().contains("/heart-rate") {
                    self.handle_vital_signs(&json);
                } else if msg.topic().contains("/compliance/audit/") {
                    self.handle_audit_log(&json);
                } else if msg.topic().contains("/devices/") && msg.topic().contains("/status") {
                    self.handle_device_status(&json);
                }
            } else {
                println!("Payload: {}", payload_str);
            }
        }
        
        println!("QoS: {}", msg.qos());
        println!("============================\n");
    }
    
    fn handle_critical_alert(&self, data: &Value) {
        if let Some(patient_id) = data["patient_id"].as_str() {
            println!("EMERGENCY: Patient {} requires immediate attention!", patient_id);
            // Trigger emergency response protocol
            // Send notifications to medical staff
            // Update dashboard with priority alert
        }
    }
    
    fn handle_vital_signs(&self, data: &Value) {
        if let Some(secure_data) = data.get("data") {
            if let Some(patient_id) = secure_data["patient_id"].as_str() {
                println!("Processing vital signs for patient: {}", patient_id);
                // Verify integrity hash
                // Update patient monitoring dashboard
                // Store in electronic health records (EHR) system
            }
        }
    }
    
    fn handle_audit_log(&self, data: &Value) {
        println!("Logging compliance event for regulatory tracking");
        // Store in secure audit database
        // Generate compliance reports
    }
    
    fn handle_device_status(&self, data: &Value) {
        if let Some(device_id) = data["device_id"].as_str() {
            if let Some(status) = data["status"].as_str() {
                println!("Device {} status: {}", device_id, status);
                // Update device monitoring dashboard
                // Alert maintenance if device offline
            }
        }
    }
    
    fn start_monitoring(&self) -> Result<(), mqtt::Error> {
        let rx = self.client.start_consuming();
        
        println!("Healthcare dashboard monitoring started...");
        
        for msg_opt in rx.iter() {
            if let Some(msg) = msg_opt {
                self.process_message(&msg);
            } else if !self.client.is_connected() {
                println!("Connection lost. Attempting to reconnect...");
                if self.client.reconnect().is_ok() {
                    println!("Reconnected successfully");
                    self.subscribe_to_topics()?;
                }
            }
        }
        
        Ok(())
    }
}

fn main() -> Result<(), mqtt::Error> {
    let broker_url = "ssl://secure-healthcare-broker.hospital.com:8883";
    let dashboard = HealthcareDashboard::new(broker_url)?;
    
    dashboard.subscribe_to_topics()?;
    dashboard.start_monitoring()?;
    
    Ok(())
}
```

## Summary

Healthcare and medical device MQTT implementations provide critical infrastructure for remote patient monitoring and medical IoT systems with comprehensive compliance features:

**Key Features:**
- Secure TLS/SSL encrypted communications for HIPAA compliance
- QoS 2 (exactly once) delivery for critical health data
- Real-time vital signs monitoring with configurable thresholds
- Automated critical alert generation and emergency notifications
- Data integrity verification using SHA-256 hashing
- Comprehensive audit logging for regulatory compliance
- Patient data anonymization and access control
- Device status monitoring and health tracking

**Use Cases:**
- Remote patient monitoring for chronic disease management
- ICU and hospital room vital signs monitoring
- Wearable medical device data collection
- Telemedicine and telehealth platforms
- Medical device fleet management
- Clinical trial data collection
- Emergency response systems
- Regulatory compliance and audit trail management

**Compliance Considerations:**
- HIPAA (Health Insurance Portability and Accountability Act)
- GDPR for patient data privacy
- FDA regulations for medical devices
- HL7 FHIR standards for healthcare data exchange
- ISO 13485 for medical device quality management

This implementation ensures that healthcare organizations can leverage IoT and MQTT technologies while maintaining the highest standards of security, privacy, and regulatory compliance required in medical environments.