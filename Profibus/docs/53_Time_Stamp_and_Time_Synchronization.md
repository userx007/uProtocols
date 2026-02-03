# Time Stamp and Time Synchronization in Profibus

## Detailed Description

Time stamping and time synchronization in Profibus are critical features for industrial automation systems that require precise timing coordination across distributed devices, event correlation, and synchronized operations. These mechanisms ensure that all devices in a Profibus network operate with a common time reference, enabling accurate event logging, coordinated control actions, and deterministic system behavior.

### Key Concepts

**Time Stamping:**
- Records the exact time when specific events occur (sensor readings, alarms, state changes)
- Enables correlation of events across multiple devices
- Supports forensic analysis and troubleshooting
- Provides audit trails for compliance and quality assurance

**Time Synchronization:**
- Establishes a common time base across all network participants
- Compensates for clock drift in individual devices
- Ensures coordinated execution of time-critical operations
- Supports synchronized data acquisition from multiple sources

**Profibus Time Synchronization Methods:**

1. **Clock Synchronization (Profibus DP):** Master broadcasts time to all slaves periodically
2. **Time Stamp Master/Slave:** Dedicated time master provides reference time
3. **SNTP/NTP Integration:** External time sources for absolute time reference
4. **PTP (Precision Time Protocol):** For sub-microsecond synchronization in modern systems

### Technical Implementation

The time synchronization typically operates through:
- Broadcast or multicast time messages from master to slaves
- Compensation for transmission delays
- Local clock adjustment algorithms
- Periodic synchronization intervals (typically 1-10 seconds)
- Accuracy typically in the range of 1-100 microseconds depending on configuration

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// Profibus time synchronization structures
#define PROFIBUS_TIME_SYNC_INTERVAL_MS 1000
#define MAX_SLAVES 32
#define TIME_ACCURACY_US 100

// Profibus timestamp format (compatible with IEC 61158)
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t microsecond;
    int16_t utc_offset_minutes; // UTC offset in minutes
} profibus_timestamp_t;

// Time synchronization message
typedef struct {
    uint8_t message_type;      // 0x01 for time sync
    profibus_timestamp_t timestamp;
    uint32_t sequence_number;
    uint16_t checksum;
} time_sync_message_t;

// Event with timestamp
typedef struct {
    uint32_t event_id;
    uint8_t event_type;
    uint8_t source_address;
    profibus_timestamp_t timestamp;
    uint8_t data[64];
    uint16_t data_length;
} timestamped_event_t;

// Time synchronization state for a slave
typedef struct {
    uint8_t address;
    int64_t offset_us;         // Time offset from master in microseconds
    uint32_t last_sync_seq;
    time_t last_sync_time;
    uint8_t sync_active;
} slave_time_state_t;

// Master time synchronization context
typedef struct {
    profibus_timestamp_t master_time;
    slave_time_state_t slaves[MAX_SLAVES];
    uint32_t sync_sequence;
    pthread_mutex_t time_lock;
    uint8_t sync_enabled;
} time_sync_master_t;

// Slave time synchronization context
typedef struct {
    uint8_t slave_address;
    profibus_timestamp_t local_time;
    int64_t master_offset_us;
    uint32_t last_received_seq;
    pthread_mutex_t time_lock;
    uint8_t synchronized;
} time_sync_slave_t;

// Initialize master time synchronization
void init_time_sync_master(time_sync_master_t* master) {
    memset(master, 0, sizeof(time_sync_master_t));
    pthread_mutex_init(&master->time_lock, NULL);
    master->sync_enabled = 1;
    master->sync_sequence = 0;
    
    // Initialize with current system time
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    master->master_time.year = tm_info->tm_year + 1900;
    master->master_time.month = tm_info->tm_mon + 1;
    master->master_time.day = tm_info->tm_mday;
    master->master_time.hour = tm_info->tm_hour;
    master->master_time.minute = tm_info->tm_min;
    master->master_time.second = tm_info->tm_sec;
    master->master_time.microsecond = 0;
}

// Initialize slave time synchronization
void init_time_sync_slave(time_sync_slave_t* slave, uint8_t address) {
    memset(slave, 0, sizeof(time_sync_slave_t));
    slave->slave_address = address;
    pthread_mutex_init(&slave->time_lock, NULL);
    slave->synchronized = 0;
    slave->master_offset_us = 0;
}

// Get current timestamp with microsecond precision
void get_current_timestamp(profibus_timestamp_t* ts) {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    
    time_t now = spec.tv_sec;
    struct tm* tm_info = localtime(&now);
    
    ts->year = tm_info->tm_year + 1900;
    ts->month = tm_info->tm_mon + 1;
    ts->day = tm_info->tm_mday;
    ts->hour = tm_info->tm_hour;
    ts->minute = tm_info->tm_min;
    ts->second = tm_info->tm_sec;
    ts->microsecond = spec.tv_nsec / 1000;
    ts->utc_offset_minutes = tm_info->tm_gmtoff / 60;
}

// Convert timestamp to microseconds since epoch
int64_t timestamp_to_microseconds(profibus_timestamp_t* ts) {
    struct tm tm_info = {0};
    tm_info.tm_year = ts->year - 1900;
    tm_info.tm_mon = ts->month - 1;
    tm_info.tm_mday = ts->day;
    tm_info.tm_hour = ts->hour;
    tm_info.tm_min = ts->minute;
    tm_info.tm_sec = ts->second;
    
    time_t seconds = mktime(&tm_info);
    return (int64_t)seconds * 1000000LL + ts->microsecond;
}

// Calculate checksum for time sync message
uint16_t calculate_time_sync_checksum(time_sync_message_t* msg) {
    uint16_t checksum = 0;
    uint8_t* data = (uint8_t*)msg;
    size_t len = sizeof(time_sync_message_t) - sizeof(uint16_t);
    
    for (size_t i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

// Master: Create and broadcast time synchronization message
int broadcast_time_sync(time_sync_master_t* master, 
                       int (*send_broadcast)(uint8_t*, size_t)) {
    pthread_mutex_lock(&master->time_lock);
    
    time_sync_message_t msg;
    msg.message_type = 0x01;
    
    // Get current precise time
    get_current_timestamp(&msg.timestamp);
    
    msg.sequence_number = ++master->sync_sequence;
    msg.checksum = calculate_time_sync_checksum(&msg);
    
    pthread_mutex_unlock(&master->time_lock);
    
    // Broadcast to all slaves
    int result = send_broadcast((uint8_t*)&msg, sizeof(msg));
    
    if (result == 0) {
        printf("Time sync broadcast: Seq=%u, Time=%04d-%02d-%02d %02d:%02d:%02d.%06u\n",
               msg.sequence_number,
               msg.timestamp.year, msg.timestamp.month, msg.timestamp.day,
               msg.timestamp.hour, msg.timestamp.minute, msg.timestamp.second,
               msg.timestamp.microsecond);
    }
    
    return result;
}

// Slave: Process received time synchronization message
int process_time_sync(time_sync_slave_t* slave, time_sync_message_t* msg) {
    // Verify checksum
    uint16_t received_checksum = msg->checksum;
    msg->checksum = 0;
    uint16_t calculated_checksum = calculate_time_sync_checksum(msg);
    
    if (received_checksum != calculated_checksum) {
        printf("Slave %d: Time sync checksum error\n", slave->slave_address);
        return -1;
    }
    
    pthread_mutex_lock(&slave->time_lock);
    
    // Get local reception time
    profibus_timestamp_t reception_time;
    get_current_timestamp(&reception_time);
    
    // Calculate offset
    int64_t master_time_us = timestamp_to_microseconds(&msg->timestamp);
    int64_t local_time_us = timestamp_to_microseconds(&reception_time);
    
    // Simple offset calculation (in production, use more sophisticated algorithms)
    slave->master_offset_us = master_time_us - local_time_us;
    slave->last_received_seq = msg->sequence_number;
    slave->synchronized = 1;
    
    // Update local synchronized time
    memcpy(&slave->local_time, &msg->timestamp, sizeof(profibus_timestamp_t));
    
    pthread_mutex_unlock(&slave->time_lock);
    
    printf("Slave %d: Synchronized. Offset=%lld us, Seq=%u\n",
           slave->slave_address, (long long)slave->master_offset_us, 
           msg->sequence_number);
    
    return 0;
}

// Slave: Get synchronized time
int get_synchronized_time(time_sync_slave_t* slave, profibus_timestamp_t* ts) {
    pthread_mutex_lock(&slave->time_lock);
    
    if (!slave->synchronized) {
        pthread_mutex_unlock(&slave->time_lock);
        return -1;
    }
    
    // Get current local time
    get_current_timestamp(ts);
    
    // Apply offset to synchronize with master
    int64_t local_us = timestamp_to_microseconds(ts);
    int64_t synchronized_us = local_us + slave->master_offset_us;
    
    // Convert back to timestamp structure
    time_t seconds = synchronized_us / 1000000LL;
    struct tm* tm_info = localtime(&seconds);
    
    ts->year = tm_info->tm_year + 1900;
    ts->month = tm_info->tm_mon + 1;
    ts->day = tm_info->tm_mday;
    ts->hour = tm_info->tm_hour;
    ts->minute = tm_info->tm_min;
    ts->second = tm_info->tm_sec;
    ts->microsecond = synchronized_us % 1000000LL;
    
    pthread_mutex_unlock(&slave->time_lock);
    return 0;
}

// Create timestamped event
void create_timestamped_event(timestamped_event_t* event,
                              uint32_t event_id,
                              uint8_t event_type,
                              uint8_t source_address,
                              time_sync_slave_t* slave) {
    event->event_id = event_id;
    event->event_type = event_type;
    event->source_address = source_address;
    
    if (slave && slave->synchronized) {
        get_synchronized_time(slave, &event->timestamp);
    } else {
        get_current_timestamp(&event->timestamp);
    }
}

// Example: Mock broadcast function (replace with actual Profibus send)
int mock_send_broadcast(uint8_t* data, size_t len) {
    // In real implementation, this would send via Profibus
    return 0; // Success
}

// Demonstration
int main() {
    printf("=== Profibus Time Stamp and Synchronization Demo ===\n\n");
    
    // Initialize master
    time_sync_master_t master;
    init_time_sync_master(&master);
    
    // Initialize slaves
    time_sync_slave_t slave1, slave2;
    init_time_sync_slave(&slave1, 3);
    init_time_sync_slave(&slave2, 5);
    
    // Simulate time synchronization cycle
    for (int cycle = 0; cycle < 3; cycle++) {
        printf("\n--- Sync Cycle %d ---\n", cycle + 1);
        
        // Master broadcasts time
        time_sync_message_t sync_msg;
        sync_msg.message_type = 0x01;
        get_current_timestamp(&sync_msg.timestamp);
        sync_msg.sequence_number = ++master.sync_sequence;
        sync_msg.checksum = calculate_time_sync_checksum(&sync_msg);
        
        // Slaves receive and process
        process_time_sync(&slave1, &sync_msg);
        process_time_sync(&slave2, &sync_msg);
        
        // Create timestamped events
        timestamped_event_t event1;
        create_timestamped_event(&event1, 100, 0x01, slave1.slave_address, &slave1);
        printf("Event from Slave %d: ID=%u at %04d-%02d-%02d %02d:%02d:%02d.%06u\n",
               slave1.slave_address, event1.event_id,
               event1.timestamp.year, event1.timestamp.month, event1.timestamp.day,
               event1.timestamp.hour, event1.timestamp.minute, 
               event1.timestamp.second, event1.timestamp.microsecond);
        
        usleep(100000); // 100ms delay
    }
    
    // Cleanup
    pthread_mutex_destroy(&master.time_lock);
    pthread_mutex_destroy(&slave1.time_lock);
    pthread_mutex_destroy(&slave2.time_lock);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}
```

---

## Rust Implementation

```rust
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use std::thread;

// Profibus timestamp structure
#[derive(Debug, Clone, Copy)]
struct ProfibusTimestamp {
    year: u16,
    month: u8,
    day: u8,
    hour: u8,
    minute: u8,
    second: u8,
    microsecond: u32,
    utc_offset_minutes: i16,
}

// Time synchronization message
#[derive(Debug, Clone)]
struct TimeSyncMessage {
    message_type: u8,
    timestamp: ProfibusTimestamp,
    sequence_number: u32,
    checksum: u16,
}

// Timestamped event
#[derive(Debug, Clone)]
struct TimestampedEvent {
    event_id: u32,
    event_type: u8,
    source_address: u8,
    timestamp: ProfibusTimestamp,
    data: Vec<u8>,
}

// Slave time state
#[derive(Debug, Clone)]
struct SlaveTimeState {
    address: u8,
    offset_us: i64,
    last_sync_seq: u32,
    last_sync_time: SystemTime,
    sync_active: bool,
}

// Master time synchronization context
struct TimeSyncMaster {
    master_time: Mutex<ProfibusTimestamp>,
    slaves: Mutex<Vec<SlaveTimeState>>,
    sync_sequence: Mutex<u32>,
    sync_enabled: bool,
}

// Slave time synchronization context
struct TimeSyncSlave {
    slave_address: u8,
    local_time: Mutex<ProfibusTimestamp>,
    master_offset_us: Mutex<i64>,
    last_received_seq: Mutex<u32>,
    synchronized: Mutex<bool>,
}

impl ProfibusTimestamp {
    // Create timestamp from current system time
    fn now() -> Self {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("Time went backwards");
        
        let total_seconds = now.as_secs();
        let microseconds = now.subsec_micros();
        
        // Simple conversion (in production, use chrono crate)
        let days_since_epoch = total_seconds / 86400;
        let seconds_today = total_seconds % 86400;
        
        let hour = (seconds_today / 3600) as u8;
        let minute = ((seconds_today % 3600) / 60) as u8;
        let second = (seconds_today % 60) as u8;
        
        // Simplified date calculation (use chrono for accurate conversion)
        let year = 1970 + (days_since_epoch / 365) as u16;
        let month = 1;
        let day = 1;
        
        ProfibusTimestamp {
            year,
            month,
            day,
            hour,
            minute,
            second,
            microsecond: microseconds,
            utc_offset_minutes: 0,
        }
    }
    
    // Convert to microseconds since epoch
    fn to_microseconds(&self) -> i64 {
        // Simplified conversion
        let base_days = (self.year as i64 - 1970) * 365;
        let total_seconds = base_days * 86400 
            + self.day as i64 * 86400
            + self.hour as i64 * 3600
            + self.minute as i64 * 60
            + self.second as i64;
        
        total_seconds * 1_000_000 + self.microsecond as i64
    }
    
    // Create from microseconds since epoch
    fn from_microseconds(us: i64) -> Self {
        let total_seconds = us / 1_000_000;
        let microseconds = (us % 1_000_000) as u32;
        
        let days_since_epoch = total_seconds / 86400;
        let seconds_today = total_seconds % 86400;
        
        let hour = (seconds_today / 3600) as u8;
        let minute = ((seconds_today % 3600) / 60) as u8;
        let second = (seconds_today % 60) as u8;
        
        let year = 1970 + (days_since_epoch / 365) as u16;
        
        ProfibusTimestamp {
            year,
            month: 1,
            day: 1,
            hour,
            minute,
            second,
            microsecond: microseconds,
            utc_offset_minutes: 0,
        }
    }
}

impl TimeSyncMessage {
    // Calculate checksum
    fn calculate_checksum(&self) -> u16 {
        let mut checksum: u16 = 0;
        checksum = checksum.wrapping_add(self.message_type as u16);
        checksum = checksum.wrapping_add(self.timestamp.year);
        checksum = checksum.wrapping_add(self.timestamp.month as u16);
        checksum = checksum.wrapping_add(self.timestamp.day as u16);
        checksum = checksum.wrapping_add(self.timestamp.hour as u16);
        checksum = checksum.wrapping_add(self.timestamp.minute as u16);
        checksum = checksum.wrapping_add(self.timestamp.second as u16);
        checksum = checksum.wrapping_add((self.timestamp.microsecond >> 16) as u16);
        checksum = checksum.wrapping_add((self.timestamp.microsecond & 0xFFFF) as u16);
        checksum = checksum.wrapping_add((self.sequence_number >> 16) as u16);
        checksum = checksum.wrapping_add((self.sequence_number & 0xFFFF) as u16);
        checksum
    }
    
    // Verify checksum
    fn verify_checksum(&self) -> bool {
        self.checksum == self.calculate_checksum()
    }
}

impl TimeSyncMaster {
    // Initialize master
    fn new() -> Self {
        TimeSyncMaster {
            master_time: Mutex::new(ProfibusTimestamp::now()),
            slaves: Mutex::new(Vec::new()),
            sync_sequence: Mutex::new(0),
            sync_enabled: true,
        }
    }
    
    // Broadcast time synchronization
    fn broadcast_time_sync(&self) -> Result<TimeSyncMessage, String> {
        let timestamp = ProfibusTimestamp::now();
        
        let mut seq = self.sync_sequence.lock().unwrap();
        *seq += 1;
        let sequence_number = *seq;
        
        let mut msg = TimeSyncMessage {
            message_type: 0x01,
            timestamp,
            sequence_number,
            checksum: 0,
        };
        
        msg.checksum = msg.calculate_checksum();
        
        println!("Time sync broadcast: Seq={}, Time={:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06}",
                 msg.sequence_number,
                 msg.timestamp.year, msg.timestamp.month, msg.timestamp.day,
                 msg.timestamp.hour, msg.timestamp.minute, msg.timestamp.second,
                 msg.timestamp.microsecond);
        
        Ok(msg)
    }
}

impl TimeSyncSlave {
    // Initialize slave
    fn new(address: u8) -> Self {
        TimeSyncSlave {
            slave_address: address,
            local_time: Mutex::new(ProfibusTimestamp::now()),
            master_offset_us: Mutex::new(0),
            last_received_seq: Mutex::new(0),
            synchronized: Mutex::new(false),
        }
    }
    
    // Process time synchronization message
    fn process_time_sync(&self, msg: &TimeSyncMessage) -> Result<(), String> {
        // Verify checksum
        if !msg.verify_checksum() {
            return Err(format!("Slave {}: Checksum error", self.slave_address));
        }
        
        // Get local reception time
        let reception_time = ProfibusTimestamp::now();
        
        // Calculate offset
        let master_time_us = msg.timestamp.to_microseconds();
        let local_time_us = reception_time.to_microseconds();
        let offset = master_time_us - local_time_us;
        
        // Update state
        *self.master_offset_us.lock().unwrap() = offset;
        *self.last_received_seq.lock().unwrap() = msg.sequence_number;
        *self.synchronized.lock().unwrap() = true;
        *self.local_time.lock().unwrap() = msg.timestamp;
        
        println!("Slave {}: Synchronized. Offset={} us, Seq={}",
                 self.slave_address, offset, msg.sequence_number);
        
        Ok(())
    }
    
    // Get synchronized time
    fn get_synchronized_time(&self) -> Result<ProfibusTimestamp, String> {
        let synchronized = *self.synchronized.lock().unwrap();
        
        if !synchronized {
            return Err("Not synchronized".to_string());
        }
        
        let local_time = ProfibusTimestamp::now();
        let offset = *self.master_offset_us.lock().unwrap();
        
        let local_us = local_time.to_microseconds();
        let synchronized_us = local_us + offset;
        
        Ok(ProfibusTimestamp::from_microseconds(synchronized_us))
    }
}

impl TimestampedEvent {
    // Create new timestamped event
    fn new(event_id: u32, event_type: u8, source_address: u8, 
           slave: Option<&TimeSyncSlave>) -> Self {
        let timestamp = if let Some(s) = slave {
            s.get_synchronized_time().unwrap_or_else(|_| ProfibusTimestamp::now())
        } else {
            ProfibusTimestamp::now()
        };
        
        TimestampedEvent {
            event_id,
            event_type,
            source_address,
            timestamp,
            data: Vec::new(),
        }
    }
}

// Demonstration
fn main() {
    println!("=== Profibus Time Stamp and Synchronization Demo (Rust) ===\n");
    
    // Initialize master
    let master = Arc::new(TimeSyncMaster::new());
    
    // Initialize slaves
    let slave1 = Arc::new(TimeSyncSlave::new(3));
    let slave2 = Arc::new(TimeSyncSlave::new(5));
    
    // Simulate synchronization cycles
    for cycle in 1..=3 {
        println!("\n--- Sync Cycle {} ---", cycle);
        
        // Master broadcasts time
        let sync_msg = master.broadcast_time_sync().unwrap();
        
        // Slaves receive and process
        slave1.process_time_sync(&sync_msg).unwrap();
        slave2.process_time_sync(&sync_msg).unwrap();
        
        // Create timestamped events
        let event1 = TimestampedEvent::new(100 + cycle, 0x01, 
                                          slave1.slave_address, Some(&slave1));
        
        println!("Event from Slave {}: ID={} at {:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06}",
                 slave1.slave_address, event1.event_id,
                 event1.timestamp.year, event1.timestamp.month, event1.timestamp.day,
                 event1.timestamp.hour, event1.timestamp.minute,
                 event1.timestamp.second, event1.timestamp.microsecond);
        
        thread::sleep(Duration::from_millis(100));
    }
    
    println!("\n=== Demo Complete ===");
}
```

---

## Summary

**Time stamping and synchronization in Profibus** enables distributed industrial systems to maintain a common time reference with microsecond-level accuracy. The master device periodically broadcasts synchronized time messages to all slaves, which adjust their local clocks to compensate for drift. This allows for:

- **Precise event correlation** across multiple devices for troubleshooting and analysis
- **Coordinated control actions** where multiple actuators must operate simultaneously
- **Accurate data logging** with timestamps that can be compared across the entire network
- **Compliance and auditing** through reliable event sequence reconstruction

The implementations demonstrate master-slave time synchronization with checksum verification, offset calculation, and timestamped event generation. In production systems, more sophisticated algorithms (like IEEE 1588 PTP) may be used for higher precision, and proper handling of network delays, clock drift compensation, and redundant time sources should be implemented for critical applications.