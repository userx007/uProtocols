# Event Logging and Alarms in Profibus Systems

## Detailed Description

Event logging and alarm management are critical components of industrial automation systems using Profibus. These systems provide operators and maintenance personnel with real-time visibility into system status, historical records of events, and immediate notification of fault conditions.

### Key Concepts

**Event Logging** involves capturing and storing timestamped records of significant occurrences in the automation system, including:
- Process state changes
- Device status transitions
- Communication errors
- Operator actions
- Configuration changes
- Maintenance activities

**Alarm Management** focuses on real-time detection, classification, and notification of abnormal conditions requiring operator intervention:
- Critical alarms (immediate safety concerns)
- Warning alarms (potential issues)
- Informational messages
- Acknowledgment tracking
- Alarm prioritization

### Profibus-Specific Considerations

Profibus networks generate various events that require logging:
- DP slave status changes (operational, diagnostic, parameterization)
- Cyclic data transmission interruptions
- Acyclic service responses
- Bus timing violations
- Device failures and recoveries
- Configuration mismatches

## Programming Implementation

### C/C++ Implementation

```cpp
// event_logger.h
#ifndef EVENT_LOGGER_H
#define EVENT_LOGGER_H

#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event severity levels
typedef enum {
    EVENT_INFO = 0,
    EVENT_WARNING = 1,
    EVENT_ERROR = 2,
    EVENT_CRITICAL = 3
} EventSeverity;

// Event categories
typedef enum {
    CAT_SYSTEM = 0,
    CAT_COMMUNICATION = 1,
    CAT_PROCESS = 2,
    CAT_DEVICE = 3,
    CAT_SECURITY = 4
} EventCategory;

// Alarm states
typedef enum {
    ALARM_INACTIVE = 0,
    ALARM_ACTIVE = 1,
    ALARM_ACKNOWLEDGED = 2,
    ALARM_CLEARED = 3
} AlarmState;

// Event structure
typedef struct {
    uint32_t event_id;
    time_t timestamp;
    EventSeverity severity;
    EventCategory category;
    uint8_t source_address;  // Profibus slave address
    char message[256];
    uint32_t data[4];  // Additional event data
} Event;

// Alarm structure
typedef struct {
    uint32_t alarm_id;
    time_t triggered_time;
    time_t acknowledged_time;
    time_t cleared_time;
    EventSeverity severity;
    AlarmState state;
    uint8_t device_address;
    char description[256];
    bool requires_ack;
    uint32_t occurrence_count;
} Alarm;

// Event logger configuration
typedef struct {
    char log_file_path[256];
    size_t max_log_size;
    size_t max_events_memory;
    bool enable_console_output;
    bool enable_file_output;
    EventSeverity min_severity;
} LoggerConfig;

// Event logger context
typedef struct {
    LoggerConfig config;
    Event *event_buffer;
    size_t event_count;
    size_t buffer_index;
    FILE *log_file;
    pthread_mutex_t lock;
    uint32_t next_event_id;
} EventLogger;

// Alarm manager context
typedef struct {
    Alarm *active_alarms;
    size_t max_alarms;
    size_t active_count;
    pthread_mutex_t lock;
    uint32_t next_alarm_id;
    void (*alarm_callback)(const Alarm *alarm);
} AlarmManager;

// Function prototypes
EventLogger* event_logger_create(const LoggerConfig *config);
void event_logger_destroy(EventLogger *logger);
int event_logger_log(EventLogger *logger, EventSeverity severity, 
                     EventCategory category, uint8_t source_addr,
                     const char *format, ...);
int event_logger_get_events(EventLogger *logger, Event *events, 
                            size_t max_events, EventSeverity min_severity);

AlarmManager* alarm_manager_create(size_t max_alarms);
void alarm_manager_destroy(AlarmManager *manager);
int alarm_trigger(AlarmManager *manager, EventSeverity severity,
                 uint8_t device_addr, const char *description,
                 bool requires_ack);
int alarm_acknowledge(AlarmManager *manager, uint32_t alarm_id);
int alarm_clear(AlarmManager *manager, uint32_t alarm_id);
int alarm_get_active(AlarmManager *manager, Alarm *alarms, size_t max_count);

#ifdef __cplusplus
}
#endif

#endif // EVENT_LOGGER_H
```

```cpp
// event_logger.c
#include "event_logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const char* severity_strings[] = {
    "INFO", "WARNING", "ERROR", "CRITICAL"
};

static const char* category_strings[] = {
    "SYSTEM", "COMMUNICATION", "PROCESS", "DEVICE", "SECURITY"
};

EventLogger* event_logger_create(const LoggerConfig *config) {
    EventLogger *logger = (EventLogger*)calloc(1, sizeof(EventLogger));
    if (!logger) return NULL;
    
    memcpy(&logger->config, config, sizeof(LoggerConfig));
    
    logger->event_buffer = (Event*)calloc(config->max_events_memory, 
                                          sizeof(Event));
    if (!logger->event_buffer) {
        free(logger);
        return NULL;
    }
    
    pthread_mutex_init(&logger->lock, NULL);
    
    if (config->enable_file_output) {
        logger->log_file = fopen(config->log_file_path, "a");
        if (!logger->log_file) {
            free(logger->event_buffer);
            free(logger);
            return NULL;
        }
    }
    
    logger->next_event_id = 1;
    return logger;
}

void event_logger_destroy(EventLogger *logger) {
    if (!logger) return;
    
    pthread_mutex_lock(&logger->lock);
    
    if (logger->log_file) {
        fclose(logger->log_file);
    }
    
    free(logger->event_buffer);
    pthread_mutex_unlock(&logger->lock);
    pthread_mutex_destroy(&logger->lock);
    free(logger);
}

int event_logger_log(EventLogger *logger, EventSeverity severity, 
                     EventCategory category, uint8_t source_addr,
                     const char *format, ...) {
    if (!logger || severity < logger->config.min_severity) return -1;
    
    pthread_mutex_lock(&logger->lock);
    
    Event *event = &logger->event_buffer[logger->buffer_index];
    event->event_id = logger->next_event_id++;
    event->timestamp = time(NULL);
    event->severity = severity;
    event->category = category;
    event->source_address = source_addr;
    
    // Format message
    va_list args;
    va_start(args, format);
    vsnprintf(event->message, sizeof(event->message), format, args);
    va_end(args);
    
    // Update buffer
    logger->buffer_index = (logger->buffer_index + 1) % 
                           logger->config.max_events_memory;
    if (logger->event_count < logger->config.max_events_memory) {
        logger->event_count++;
    }
    
    // Console output
    if (logger->config.enable_console_output) {
        struct tm *tm_info = localtime(&event->timestamp);
        char time_str[26];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        printf("[%s] [%s] [%s] [Addr:%d] %s\n",
               time_str,
               severity_strings[severity],
               category_strings[category],
               source_addr,
               event->message);
    }
    
    // File output
    if (logger->config.enable_file_output && logger->log_file) {
        struct tm *tm_info = localtime(&event->timestamp);
        char time_str[26];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(logger->log_file, "%u,%s,%s,%s,%d,%s\n",
                event->event_id,
                time_str,
                severity_strings[severity],
                category_strings[category],
                source_addr,
                event->message);
        fflush(logger->log_file);
    }
    
    pthread_mutex_unlock(&logger->lock);
    return 0;
}

// Alarm Manager Implementation
AlarmManager* alarm_manager_create(size_t max_alarms) {
    AlarmManager *manager = (AlarmManager*)calloc(1, sizeof(AlarmManager));
    if (!manager) return NULL;
    
    manager->active_alarms = (Alarm*)calloc(max_alarms, sizeof(Alarm));
    if (!manager->active_alarms) {
        free(manager);
        return NULL;
    }
    
    manager->max_alarms = max_alarms;
    pthread_mutex_init(&manager->lock, NULL);
    manager->next_alarm_id = 1;
    
    return manager;
}

void alarm_manager_destroy(AlarmManager *manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->lock);
    free(manager->active_alarms);
    pthread_mutex_unlock(&manager->lock);
    pthread_mutex_destroy(&manager->lock);
    free(manager);
}

int alarm_trigger(AlarmManager *manager, EventSeverity severity,
                 uint8_t device_addr, const char *description,
                 bool requires_ack) {
    if (!manager || manager->active_count >= manager->max_alarms) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->lock);
    
    // Check for duplicate active alarm
    for (size_t i = 0; i < manager->active_count; i++) {
        Alarm *alarm = &manager->active_alarms[i];
        if (alarm->device_address == device_addr && 
            alarm->state == ALARM_ACTIVE &&
            strcmp(alarm->description, description) == 0) {
            // Increment occurrence count for existing alarm
            alarm->occurrence_count++;
            pthread_mutex_unlock(&manager->lock);
            return alarm->alarm_id;
        }
    }
    
    // Create new alarm
    Alarm *alarm = &manager->active_alarms[manager->active_count];
    alarm->alarm_id = manager->next_alarm_id++;
    alarm->triggered_time = time(NULL);
    alarm->severity = severity;
    alarm->state = ALARM_ACTIVE;
    alarm->device_address = device_addr;
    strncpy(alarm->description, description, sizeof(alarm->description) - 1);
    alarm->requires_ack = requires_ack;
    alarm->occurrence_count = 1;
    
    manager->active_count++;
    
    // Trigger callback
    if (manager->alarm_callback) {
        manager->alarm_callback(alarm);
    }
    
    pthread_mutex_unlock(&manager->lock);
    return alarm->alarm_id;
}

int alarm_acknowledge(AlarmManager *manager, uint32_t alarm_id) {
    if (!manager) return -1;
    
    pthread_mutex_lock(&manager->lock);
    
    for (size_t i = 0; i < manager->active_count; i++) {
        Alarm *alarm = &manager->active_alarms[i];
        if (alarm->alarm_id == alarm_id && alarm->state == ALARM_ACTIVE) {
            alarm->state = ALARM_ACKNOWLEDGED;
            alarm->acknowledged_time = time(NULL);
            pthread_mutex_unlock(&manager->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&manager->lock);
    return -1;
}

int alarm_clear(AlarmManager *manager, uint32_t alarm_id) {
    if (!manager) return -1;
    
    pthread_mutex_lock(&manager->lock);
    
    for (size_t i = 0; i < manager->active_count; i++) {
        Alarm *alarm = &manager->active_alarms[i];
        if (alarm->alarm_id == alarm_id) {
            alarm->state = ALARM_CLEARED;
            alarm->cleared_time = time(NULL);
            
            // Remove from active list
            memmove(&manager->active_alarms[i],
                   &manager->active_alarms[i + 1],
                   (manager->active_count - i - 1) * sizeof(Alarm));
            manager->active_count--;
            
            pthread_mutex_unlock(&manager->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&manager->lock);
    return -1;
}
```

```cpp
// profibus_integration.cpp
#include "event_logger.h"
#include <iostream>

class ProfibusEventHandler {
private:
    EventLogger *event_logger_;
    AlarmManager *alarm_manager_;
    
public:
    ProfibusEventHandler(EventLogger *logger, AlarmManager *manager)
        : event_logger_(logger), alarm_manager_(manager) {}
    
    void handleSlaveStatusChange(uint8_t slave_addr, uint8_t old_status, 
                                 uint8_t new_status) {
        if (new_status == 0x00) {  // Slave offline
            event_logger_log(event_logger_, EVENT_ERROR, CAT_DEVICE, 
                           slave_addr, "Slave %d went offline", slave_addr);
            alarm_trigger(alarm_manager_, EVENT_ERROR, slave_addr,
                         "Device offline", true);
        } else if (old_status == 0x00 && new_status != 0x00) {
            event_logger_log(event_logger_, EVENT_INFO, CAT_DEVICE,
                           slave_addr, "Slave %d came online", slave_addr);
            // Clear previous offline alarms
        }
    }
    
    void handleCommunicationError(uint8_t slave_addr, uint16_t error_code) {
        event_logger_log(event_logger_, EVENT_WARNING, CAT_COMMUNICATION,
                       slave_addr, "Communication error 0x%04X on slave %d",
                       error_code, slave_addr);
        
        if (error_code & 0x8000) {  // Critical error bit
            alarm_trigger(alarm_manager_, EVENT_CRITICAL, slave_addr,
                         "Critical communication error", true);
        }
    }
    
    void handleDiagnosticData(uint8_t slave_addr, const uint8_t *diag_data,
                             size_t diag_len) {
        // Parse standard Profibus diagnostic data
        if (diag_len >= 6) {
            uint8_t station_status1 = diag_data[0];
            uint8_t station_status2 = diag_data[1];
            uint8_t station_status3 = diag_data[2];
            
            // Check for specific diagnostic flags
            if (station_status1 & 0x01) {  // Station not ready
                event_logger_log(event_logger_, EVENT_WARNING, CAT_DEVICE,
                               slave_addr, "Station %d not ready", slave_addr);
            }
            
            if (station_status1 & 0x08) {  // Configuration fault
                event_logger_log(event_logger_, EVENT_ERROR, CAT_DEVICE,
                               slave_addr, "Configuration fault on station %d",
                               slave_addr);
                alarm_trigger(alarm_manager_, EVENT_ERROR, slave_addr,
                             "Configuration mismatch", true);
            }
            
            if (station_status2 & 0x04) {  // Static diagnosis
                event_logger_log(event_logger_, EVENT_WARNING, CAT_DEVICE,
                               slave_addr, "Static diagnostic on station %d",
                               slave_addr);
            }
        }
    }
};

// Example usage
int main() {
    // Configure logger
    LoggerConfig log_config = {
        .log_file_path = "/var/log/profibus_events.log",
        .max_log_size = 10 * 1024 * 1024,  // 10 MB
        .max_events_memory = 10000,
        .enable_console_output = true,
        .enable_file_output = true,
        .min_severity = EVENT_INFO
    };
    
    EventLogger *logger = event_logger_create(&log_config);
    AlarmManager *alarm_mgr = alarm_manager_create(100);
    
    ProfibusEventHandler handler(logger, alarm_mgr);
    
    // Simulate events
    handler.handleSlaveStatusChange(3, 0x02, 0x00);  // Device offline
    handler.handleCommunicationError(3, 0x8001);
    
    // Wait for acknowledgment
    Alarm active_alarms[100];
    int count = alarm_get_active(alarm_mgr, active_alarms, 100);
    
    std::cout << "Active alarms: " << count << std::endl;
    for (int i = 0; i < count; i++) {
        std::cout << "Alarm ID: " << active_alarms[i].alarm_id 
                  << " - " << active_alarms[i].description << std::endl;
        
        // Acknowledge
        alarm_acknowledge(alarm_mgr, active_alarms[i].alarm_id);
    }
    
    // Cleanup
    event_logger_destroy(logger);
    alarm_manager_destroy(alarm_mgr);
    
    return 0;
}
```

### Rust Implementation

```rust
// event_logger.rs
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::collections::VecDeque;
use std::fs::OpenOptions;
use std::io::Write;
use std::sync::{Arc, Mutex};
use thiserror::Error;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum EventSeverity {
    Info = 0,
    Warning = 1,
    Error = 2,
    Critical = 3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum EventCategory {
    System,
    Communication,
    Process,
    Device,
    Security,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum AlarmState {
    Inactive,
    Active,
    Acknowledged,
    Cleared,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Event {
    pub event_id: u32,
    pub timestamp: DateTime<Utc>,
    pub severity: EventSeverity,
    pub category: EventCategory,
    pub source_address: u8,
    pub message: String,
    pub data: Vec<u32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Alarm {
    pub alarm_id: u32,
    pub triggered_time: DateTime<Utc>,
    pub acknowledged_time: Option<DateTime<Utc>>,
    pub cleared_time: Option<DateTime<Utc>>,
    pub severity: EventSeverity,
    pub state: AlarmState,
    pub device_address: u8,
    pub description: String,
    pub requires_ack: bool,
    pub occurrence_count: u32,
}

#[derive(Debug, Error)]
pub enum LoggerError {
    #[error("Buffer full")]
    BufferFull,
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Alarm not found")]
    AlarmNotFound,
}

pub struct LoggerConfig {
    pub log_file_path: String,
    pub max_events_memory: usize,
    pub enable_console_output: bool,
    pub enable_file_output: bool,
    pub min_severity: EventSeverity,
}

pub struct EventLogger {
    config: LoggerConfig,
    event_buffer: Arc<Mutex<VecDeque<Event>>>,
    next_event_id: Arc<Mutex<u32>>,
}

impl EventLogger {
    pub fn new(config: LoggerConfig) -> Self {
        EventLogger {
            config,
            event_buffer: Arc::new(Mutex::new(VecDeque::new())),
            next_event_id: Arc::new(Mutex::new(1)),
        }
    }

    pub fn log(
        &self,
        severity: EventSeverity,
        category: EventCategory,
        source_address: u8,
        message: String,
    ) -> Result<u32, LoggerError> {
        if severity < self.config.min_severity {
            return Ok(0);
        }

        let event_id = {
            let mut id = self.next_event_id.lock().unwrap();
            let current_id = *id;
            *id += 1;
            current_id
        };

        let event = Event {
            event_id,
            timestamp: Utc::now(),
            severity,
            category,
            source_address,
            message,
            data: Vec::new(),
        };

        // Add to buffer
        {
            let mut buffer = self.event_buffer.lock().unwrap();
            if buffer.len() >= self.config.max_events_memory {
                buffer.pop_front();
            }
            buffer.push_back(event.clone());
        }

        // Console output
        if self.config.enable_console_output {
            println!(
                "[{}] [{:?}] [{:?}] [Addr:{}] {}",
                event.timestamp.format("%Y-%m-%d %H:%M:%S"),
                event.severity,
                event.category,
                event.source_address,
                event.message
            );
        }

        // File output
        if self.config.enable_file_output {
            self.write_to_file(&event)?;
        }

        Ok(event_id)
    }

    fn write_to_file(&self, event: &Event) -> Result<(), LoggerError> {
        let mut file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.config.log_file_path)?;

        writeln!(
            file,
            "{},{},{:?},{:?},{},{}",
            event.event_id,
            event.timestamp.format("%Y-%m-%d %H:%M:%S"),
            event.severity,
            event.category,
            event.source_address,
            event.message
        )?;

        Ok(())
    }

    pub fn get_events(&self, min_severity: EventSeverity) -> Vec<Event> {
        let buffer = self.event_buffer.lock().unwrap();
        buffer
            .iter()
            .filter(|e| e.severity >= min_severity)
            .cloned()
            .collect()
    }
}

pub struct AlarmManager {
    active_alarms: Arc<Mutex<Vec<Alarm>>>,
    next_alarm_id: Arc<Mutex<u32>>,
    max_alarms: usize,
}

impl AlarmManager {
    pub fn new(max_alarms: usize) -> Self {
        AlarmManager {
            active_alarms: Arc::new(Mutex::new(Vec::new())),
            next_alarm_id: Arc::new(Mutex::new(1)),
            max_alarms,
        }
    }

    pub fn trigger_alarm(
        &self,
        severity: EventSeverity,
        device_address: u8,
        description: String,
        requires_ack: bool,
    ) -> Result<u32, LoggerError> {
        let mut alarms = self.active_alarms.lock().unwrap();

        // Check for duplicate
        for alarm in alarms.iter_mut() {
            if alarm.device_address == device_address
                && alarm.state == AlarmState::Active
                && alarm.description == description
            {
                alarm.occurrence_count += 1;
                return Ok(alarm.alarm_id);
            }
        }

        if alarms.len() >= self.max_alarms {
            return Err(LoggerError::BufferFull);
        }

        let alarm_id = {
            let mut id = self.next_alarm_id.lock().unwrap();
            let current_id = *id;
            *id += 1;
            current_id
        };

        let alarm = Alarm {
            alarm_id,
            triggered_time: Utc::now(),
            acknowledged_time: None,
            cleared_time: None,
            severity,
            state: AlarmState::Active,
            device_address,
            description,
            requires_ack,
            occurrence_count: 1,
        };

        alarms.push(alarm);
        Ok(alarm_id)
    }

    pub fn acknowledge_alarm(&self, alarm_id: u32) -> Result<(), LoggerError> {
        let mut alarms = self.active_alarms.lock().unwrap();

        for alarm in alarms.iter_mut() {
            if alarm.alarm_id == alarm_id && alarm.state == AlarmState::Active {
                alarm.state = AlarmState::Acknowledged;
                alarm.acknowledged_time = Some(Utc::now());
                return Ok(());
            }
        }

        Err(LoggerError::AlarmNotFound)
    }

    pub fn clear_alarm(&self, alarm_id: u32) -> Result<(), LoggerError> {
        let mut alarms = self.active_alarms.lock().unwrap();

        if let Some(pos) = alarms.iter().position(|a| a.alarm_id == alarm_id) {
            alarms[pos].state = AlarmState::Cleared;
            alarms[pos].cleared_time = Some(Utc::now());
            alarms.remove(pos);
            Ok(())
        } else {
            Err(LoggerError::AlarmNotFound)
        }
    }

    pub fn get_active_alarms(&self) -> Vec<Alarm> {
        let alarms = self.active_alarms.lock().unwrap();
        alarms.clone()
    }
}

// Profibus integration
pub struct ProfibusEventHandler {
    event_logger: Arc<EventLogger>,
    alarm_manager: Arc<AlarmManager>,
}

impl ProfibusEventHandler {
    pub fn new(event_logger: Arc<EventLogger>, alarm_manager: Arc<AlarmManager>) -> Self {
        ProfibusEventHandler {
            event_logger,
            alarm_manager,
        }
    }

    pub fn handle_slave_status_change(
        &self,
        slave_addr: u8,
        old_status: u8,
        new_status: u8,
    ) -> Result<(), LoggerError> {
        if new_status == 0x00 {
            // Slave offline
            self.event_logger.log(
                EventSeverity::Error,
                EventCategory::Device,
                slave_addr,
                format!("Slave {} went offline", slave_addr),
            )?;
            self.alarm_manager.trigger_alarm(
                EventSeverity::Error,
                slave_addr,
                "Device offline".to_string(),
                true,
            )?;
        } else if old_status == 0x00 && new_status != 0x00 {
            self.event_logger.log(
                EventSeverity::Info,
                EventCategory::Device,
                slave_addr,
                format!("Slave {} came online", slave_addr),
            )?;
        }

        Ok(())
    }

    pub fn handle_diagnostic_data(
        &self,
        slave_addr: u8,
        diag_data: &[u8],
    ) -> Result<(), LoggerError> {
        if diag_data.len() >= 6 {
            let station_status1 = diag_data[0];

            if station_status1 & 0x01 != 0 {
                self.event_logger.log(
                    EventSeverity::Warning,
                    EventCategory::Device,
                    slave_addr,
                    format!("Station {} not ready", slave_addr),
                )?;
            }

            if station_status1 & 0x08 != 0 {
                self.event_logger.log(
                    EventSeverity::Error,
                    EventCategory::Device,
                    slave_addr,
                    format!("Configuration fault on station {}", slave_addr),
                )?;
                self.alarm_manager.trigger_alarm(
                    EventSeverity::Error,
                    slave_addr,
                    "Configuration mismatch".to_string(),
                    true,
                )?;
            }
        }

        Ok(())
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_event_logging_and_alarms() {
        let config = LoggerConfig {
            log_file_path: "/tmp/profibus_events.log".to_string(),
            max_events_memory: 1000,
            enable_console_output: true,
            enable_file_output: true,
            min_severity: EventSeverity::Info,
        };

        let logger = Arc::new(EventLogger::new(config));
        let alarm_mgr = Arc::new(AlarmManager::new(100));
        let handler = ProfibusEventHandler::new(logger.clone(), alarm_mgr.clone());

        // Simulate events
        handler.handle_slave_status_change(3, 0x02, 0x00).unwrap();

        let alarms = alarm_mgr.get_active_alarms();
        assert_eq!(alarms.len(), 1);

        alarm_mgr.acknowledge_alarm(alarms[0].alarm_id).unwrap();
        alarm_mgr.clear_alarm(alarms[0].alarm_id).unwrap();

        let events = logger.get_events(EventSeverity::Info);
        assert!(!events.is_empty());
    }
}
```

## Summary

Event logging and alarm management in Profibus systems provide essential capabilities for monitoring, troubleshooting, and maintaining industrial automation networks. The implementations above demonstrate:

**Key Features:**
- Thread-safe event logging with configurable severity levels and categories
- Circular buffer for in-memory event storage with file persistence
- Alarm management with state tracking (active, acknowledged, cleared)
- Duplicate alarm detection with occurrence counting
- Integration with Profibus diagnostic data and status changes

**Best Practices:**
- Use appropriate severity levels to avoid alarm flooding
- Implement alarm acknowledgment workflows for critical events
- Store timestamped events for historical analysis and compliance
- Parse Profibus diagnostic data to trigger relevant alarms
- Provide operator-friendly alarm descriptions
- Log both communication and device-level events

Both C/C++ and Rust implementations offer production-ready solutions with mutex-based thread safety, configurable logging levels, and comprehensive alarm lifecycle management suitable for industrial Profibus applications.