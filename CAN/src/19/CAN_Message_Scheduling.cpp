#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

// CAN message structure
typedef struct {
    uint32_t id;           // CAN identifier (priority)
    uint8_t data[8];       // Data payload
    uint8_t dlc;           // Data length code
    uint32_t timestamp;    // Last transmission time (ms)
} can_message_t;

// Message schedule entry
typedef struct {
    can_message_t msg;
    uint32_t period_ms;    // Transmission period (0 = event-triggered)
    uint32_t deadline_ms;  // Message deadline
    bool (*trigger_fn)(void); // Event trigger function (NULL for time-triggered)
    void (*data_fn)(uint8_t*); // Function to populate data
} schedule_entry_t;

// Scheduling modes
typedef enum {
    SCHED_TIME_TRIGGERED,
    SCHED_EVENT_TRIGGERED,
    SCHED_HYBRID
} schedule_mode_t;

// Simple CAN transmit function (platform-specific implementation needed)
extern bool can_transmit(can_message_t* msg);

// Get current time in milliseconds
uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

// Example: Time-triggered message scheduler
void time_triggered_scheduler(schedule_entry_t* schedule, size_t count) {
    uint32_t current_time = get_time_ms();
    
    for (size_t i = 0; i < count; i++) {
        schedule_entry_t* entry = &schedule[i];
        
        // Check if message period has elapsed
        if (entry->period_ms > 0) {
            uint32_t elapsed = current_time - entry->msg.timestamp;
            
            if (elapsed >= entry->period_ms) {
                // Update message data if function provided
                if (entry->data_fn) {
                    entry->data_fn(entry->msg.data);
                }
                
                // Transmit message
                if (can_transmit(&entry->msg)) {
                    entry->msg.timestamp = current_time;
                }
            }
        }
    }
}

// Example: Event-triggered message scheduler
void event_triggered_scheduler(schedule_entry_t* schedule, size_t count) {
    uint32_t current_time = get_time_ms();
    
    for (size_t i = 0; i < count; i++) {
        schedule_entry_t* entry = &schedule[i];
        
        // Check if event condition is met
        if (entry->trigger_fn && entry->trigger_fn()) {
            // Update message data
            if (entry->data_fn) {
                entry->data_fn(entry->msg.data);
            }
            
            // Check deadline hasn't passed
            uint32_t time_since_last = current_time - entry->msg.timestamp;
            if (time_since_last < entry->deadline_ms) {
                if (can_transmit(&entry->msg)) {
                    entry->msg.timestamp = current_time;
                }
            }
        }
    }
}

// Priority queue for hybrid scheduling (simple array-based)
#define MAX_QUEUE_SIZE 32

typedef struct {
    can_message_t messages[MAX_QUEUE_SIZE];
    uint8_t count;
} priority_queue_t;

void pq_init(priority_queue_t* pq) {
    pq->count = 0;
}

// Insert message maintaining priority order (lower ID = higher priority)
bool pq_insert(priority_queue_t* pq, can_message_t* msg) {
    if (pq->count >= MAX_QUEUE_SIZE) return false;
    
    // Find insertion point
    int i = pq->count - 1;
    while (i >= 0 && pq->messages[i].id > msg->id) {
        pq->messages[i + 1] = pq->messages[i];
        i--;
    }
    
    pq->messages[i + 1] = *msg;
    pq->count++;
    return true;
}

can_message_t* pq_pop(priority_queue_t* pq) {
    if (pq->count == 0) return NULL;
    
    static can_message_t msg;
    msg = pq->messages[0];
    
    // Shift remaining messages
    for (int i = 0; i < pq->count - 1; i++) {
        pq->messages[i] = pq->messages[i + 1];
    }
    pq->count--;
    
    return &msg;
}

// Hybrid scheduler with priority queue
priority_queue_t tx_queue;

void hybrid_scheduler(schedule_entry_t* schedule, size_t count) {
    uint32_t current_time = get_time_ms();
    
    // Phase 1: Check all schedule entries
    for (size_t i = 0; i < count; i++) {
        schedule_entry_t* entry = &schedule[i];
        bool should_send = false;
        
        // Time-triggered check
        if (entry->period_ms > 0) {
            uint32_t elapsed = current_time - entry->msg.timestamp;
            if (elapsed >= entry->period_ms) {
                should_send = true;
            }
        }
        
        // Event-triggered check
        if (entry->trigger_fn && entry->trigger_fn()) {
            should_send = true;
        }
        
        if (should_send) {
            // Update data
            if (entry->data_fn) {
                entry->data_fn(entry->msg.data);
            }
            
            // Add to priority queue
            pq_insert(&tx_queue, &entry->msg);
            entry->msg.timestamp = current_time;
        }
    }
    
    // Phase 2: Transmit from priority queue
    can_message_t* msg;
    while ((msg = pq_pop(&tx_queue)) != NULL) {
        can_transmit(msg);
    }
}

// Example usage and trigger functions
bool button_pressed = false;

bool button_trigger(void) {
    bool trigger = button_pressed;
    button_pressed = false; // Clear flag
    return trigger;
}

void engine_rpm_data(uint8_t* data) {
    // Simulated RPM value: 3500 RPM
    uint16_t rpm = 3500;
    data[0] = (rpm >> 8) & 0xFF;
    data[1] = rpm & 0xFF;
}

void temperature_data(uint8_t* data) {
    // Simulated temperature: 85°C
    data[0] = 85;
}

// Application example
void setup_message_schedule(void) {
    static schedule_entry_t schedule[] = {
        // High-priority engine RPM (time-triggered, 10ms)
        {
            .msg = {.id = 0x100, .dlc = 2, .timestamp = 0},
            .period_ms = 10,
            .deadline_ms = 15,
            .trigger_fn = NULL,
            .data_fn = engine_rpm_data
        },
        // Medium-priority temperature (time-triggered, 100ms)
        {
            .msg = {.id = 0x200, .dlc = 1, .timestamp = 0},
            .period_ms = 100,
            .deadline_ms = 150,
            .trigger_fn = NULL,
            .data_fn = temperature_data
        },
        // Low-priority button event (event-triggered)
        {
            .msg = {.id = 0x400, .dlc = 1, .timestamp = 0},
            .period_ms = 0,
            .deadline_ms = 50,
            .trigger_fn = button_trigger,
            .data_fn = NULL
        }
    };
    
    pq_init(&tx_queue);
    
    // Main scheduling loop
    while (1) {
        hybrid_scheduler(schedule, 3);
        // Small delay to prevent busy-waiting
        usleep(1000); // 1ms
    }
}