#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CAN message structure
typedef struct {
    uint32_t id;           // CAN identifier
    uint8_t dlc;           // Data length code (0-8)
    uint8_t data[8];       // Payload data
    uint32_t timestamp;    // Optional timestamp
    bool is_extended;      // Extended ID flag
    bool is_rtr;           // Remote transmission request
} can_msg_t;

// Ring buffer configuration
#define CAN_TX_BUFFER_SIZE 32
#define CAN_RX_BUFFER_SIZE 64

// Ring buffer structure
typedef struct {
    can_msg_t *buffer;     // Pointer to buffer array
    volatile uint32_t head;    // Write position
    volatile uint32_t tail;    // Read position
    uint32_t size;         // Buffer capacity
    volatile uint32_t count;   // Current number of items
} can_ring_buffer_t;

// Buffer instances
static can_msg_t tx_buffer_storage[CAN_TX_BUFFER_SIZE];
static can_msg_t rx_buffer_storage[CAN_RX_BUFFER_SIZE];

static can_ring_buffer_t tx_ring = {
    .buffer = tx_buffer_storage,
    .head = 0,
    .tail = 0,
    .size = CAN_TX_BUFFER_SIZE,
    .count = 0
};

static can_ring_buffer_t rx_ring = {
    .buffer = rx_buffer_storage,
    .head = 0,
    .tail = 0,
    .size = CAN_RX_BUFFER_SIZE,
    .count = 0
};

/**
 * Initialize a ring buffer
 */
void can_ring_init(can_ring_buffer_t *ring) {
    ring->head = 0;
    ring->tail = 0;
    ring->count = 0;
}

/**
 * Check if buffer is empty
 */
static inline bool can_ring_is_empty(const can_ring_buffer_t *ring) {
    return ring->count == 0;
}

/**
 * Check if buffer is full
 */
static inline bool can_ring_is_full(const can_ring_buffer_t *ring) {
    return ring->count >= ring->size;
}

/**
 * Get number of items in buffer
 */
static inline uint32_t can_ring_count(const can_ring_buffer_t *ring) {
    return ring->count;
}

/**
 * Get available space in buffer
 */
static inline uint32_t can_ring_space(const can_ring_buffer_t *ring) {
    return ring->size - ring->count;
}

/**
 * Push a message onto the ring buffer
 * Returns: true on success, false if buffer is full
 * 
 * This function is interrupt-safe for single producer scenarios
 */
bool can_ring_push(can_ring_buffer_t *ring, const can_msg_t *msg) {
    // Check if buffer is full
    if (can_ring_is_full(ring)) {
        return false;
    }
    
    // Copy message to buffer
    memcpy(&ring->buffer[ring->head], msg, sizeof(can_msg_t));
    
    // Advance head with wrap-around
    ring->head = (ring->head + 1) % ring->size;
    
    // Atomically increment count
    __atomic_fetch_add(&ring->count, 1, __ATOMIC_SEQ_CST);
    
    return true;
}

/**
 * Pop a message from the ring buffer
 * Returns: true on success, false if buffer is empty
 */
bool can_ring_pop(can_ring_buffer_t *ring, can_msg_t *msg) {
    // Check if buffer is empty
    if (can_ring_is_empty(ring)) {
        return false;
    }
    
    // Copy message from buffer
    memcpy(msg, &ring->buffer[ring->tail], sizeof(can_msg_t));
    
    // Advance tail with wrap-around
    ring->tail = (ring->tail + 1) % ring->size;
    
    // Atomically decrement count
    __atomic_fetch_sub(&ring->count, 1, __ATOMIC_SEQ_CST);
    
    return true;
}

/**
 * Peek at the next message without removing it
 */
bool can_ring_peek(const can_ring_buffer_t *ring, can_msg_t *msg) {
    if (can_ring_is_empty(ring)) {
        return false;
    }
    
    memcpy(msg, &ring->buffer[ring->tail], sizeof(can_msg_t));
    return true;
}

/**
 * Clear all messages from buffer
 */
void can_ring_clear(can_ring_buffer_t *ring) {
    ring->head = 0;
    ring->tail = 0;
    ring->count = 0;
}

// ============================================================================
// CAN Driver Integration Examples
// ============================================================================

/**
 * CAN TX interrupt handler
 * Called when hardware is ready to transmit
 */
void CAN_TX_IRQHandler(void) {
    can_msg_t msg;
    
    // Try to get next message from TX queue
    if (can_ring_pop(&tx_ring, &msg)) {
        // Load message into hardware registers
        CAN->TxMailbox[0].TIR = (msg.id << 21) | (msg.is_extended ? (1 << 2) : 0);
        CAN->TxMailbox[0].TDTR = msg.dlc;
        CAN->TxMailbox[0].TDLR = ((uint32_t)msg.data[3] << 24) |
                                  ((uint32_t)msg.data[2] << 16) |
                                  ((uint32_t)msg.data[1] << 8) |
                                  ((uint32_t)msg.data[0]);
        CAN->TxMailbox[0].TDHR = ((uint32_t)msg.data[7] << 24) |
                                  ((uint32_t)msg.data[6] << 16) |
                                  ((uint32_t)msg.data[5] << 8) |
                                  ((uint32_t)msg.data[4]);
        
        // Trigger transmission
        CAN->TxMailbox[0].TIR |= (1 << 0); // TXRQ bit
    } else {
        // No more messages - disable TX interrupt
        CAN->IER &= ~CAN_IER_TMEIE;
    }
}

/**
 * CAN RX interrupt handler
 * Called when a message is received
 */
void CAN_RX_IRQHandler(void) {
    can_msg_t msg;
    
    // Read message from hardware
    msg.id = (CAN->RxFIFO[0].RIR >> 21) & 0x7FF;
    msg.is_extended = (CAN->RxFIFO[0].RIR & (1 << 2)) != 0;
    msg.dlc = CAN->RxFIFO[0].RDTR & 0x0F;
    
    uint32_t data_low = CAN->RxFIFO[0].RDLR;
    uint32_t data_high = CAN->RxFIFO[0].RDHR;
    
    msg.data[0] = (data_low) & 0xFF;
    msg.data[1] = (data_low >> 8) & 0xFF;
    msg.data[2] = (data_low >> 16) & 0xFF;
    msg.data[3] = (data_low >> 24) & 0xFF;
    msg.data[4] = (data_high) & 0xFF;
    msg.data[5] = (data_high >> 8) & 0xFF;
    msg.data[6] = (data_high >> 16) & 0xFF;
    msg.data[7] = (data_high >> 24) & 0xFF;
    
    msg.timestamp = get_system_tick();
    
    // Release FIFO
    CAN->RF0R |= CAN_RF0R_RFOM0;
    
    // Queue message (drops if full)
    if (!can_ring_push(&rx_ring, &msg)) {
        // Handle overflow - could increment error counter
        rx_overflow_count++;
    }
}

/**
 * Application-level transmit function
 */
bool can_send_message(uint32_t id, const uint8_t *data, uint8_t len) {
    can_msg_t msg = {
        .id = id,
        .dlc = len,
        .is_extended = false,
        .is_rtr = false
    };
    
    memcpy(msg.data, data, len);
    
    // Try to queue message
    if (!can_ring_push(&tx_ring, &msg)) {
        return false; // Queue full
    }
    
    // Enable TX interrupt to start transmission
    CAN->IER |= CAN_IER_TMEIE;
    
    return true;
}

/**
 * Application-level receive function (non-blocking)
 */
bool can_receive_message(can_msg_t *msg) {
    return can_ring_pop(&rx_ring, msg);
}

/**
 * Example: Processing received messages in main loop
 */
void process_can_messages(void) {
    can_msg_t msg;
    
    while (can_receive_message(&msg)) {
        // Process message based on ID
        switch (msg.id) {
            case 0x100:
                handle_sensor_data(&msg);
                break;
            case 0x200:
                handle_command(&msg);
                break;
            default:
                // Unknown message
                break;
        }
    }
}

/**
 * Example: Monitoring buffer usage
 */
void monitor_buffers(void) {
    uint32_t tx_usage = (can_ring_count(&tx_ring) * 100) / tx_ring.size;
    uint32_t rx_usage = (can_ring_count(&rx_ring) * 100) / rx_ring.size;
    
    if (tx_usage > 80) {
        // TX buffer getting full - may need to increase size
        log_warning("TX buffer high: %u%%", tx_usage);
    }
    
    if (rx_usage > 80) {
        // RX buffer getting full - application not processing fast enough
        log_warning("RX buffer high: %u%%", rx_usage);
    }
}