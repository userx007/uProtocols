#include <cstdint>
#include <cstring>
#include <atomic>
#include <optional>
#include <array>

// CAN message structure
struct CanMessage {
    uint32_t id;
    uint8_t dlc;
    std::array<uint8_t, 8> data;
    uint32_t timestamp;
    bool is_extended;
    bool is_rtr;
    
    CanMessage() : id(0), dlc(0), data{}, timestamp(0), 
                   is_extended(false), is_rtr(false) {}
};

/**
 * Template-based ring buffer for type safety and compile-time sizing
 * Thread-safe for single producer, single consumer scenarios
 */
template<typename T, size_t Size>
class RingBuffer {
public:
    RingBuffer() : head_(0), tail_(0), count_(0) {}
    
    // Non-copyable to prevent accidental duplication
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    /**
     * Push an item onto the buffer
     * Returns true on success, false if buffer is full
     */
    bool push(const T& item) {
        if (is_full()) {
            return false;
        }
        
        buffer_[head_] = item;
        head_ = (head_ + 1) % Size;
        count_.fetch_add(1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * Push with move semantics for efficiency
     */
    bool push(T&& item) {
        if (is_full()) {
            return false;
        }
        
        buffer_[head_] = std::move(item);
        head_ = (head_ + 1) % Size;
        count_.fetch_add(1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * Pop an item from the buffer
     * Returns std::optional with the item, or empty if buffer is empty
     */
    std::optional<T> pop() {
        if (is_empty()) {
            return std::nullopt;
        }
        
        T item = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) % Size;
        count_.fetch_sub(1, std::memory_order_release);
        
        return item;
    }
    
    /**
     * Peek at the next item without removing it
     */
    std::optional<T> peek() const {
        if (is_empty()) {
            return std::nullopt;
        }
        return buffer_[tail_];
    }
    
    /**
     * Check if buffer is empty
     */
    bool is_empty() const {
        return count_.load(std::memory_order_acquire) == 0;
    }
    
    /**
     * Check if buffer is full
     */
    bool is_full() const {
        return count_.load(std::memory_order_acquire) >= Size;
    }
    
    /**
     * Get current number of items
     */
    size_t count() const {
        return count_.load(std::memory_order_acquire);
    }
    
    /**
     * Get buffer capacity
     */
    constexpr size_t capacity() const {
        return Size;
    }
    
    /**
     * Get available space
     */
    size_t available() const {
        return Size - count();
    }
    
    /**
     * Clear the buffer
     */
    void clear() {
        head_ = 0;
        tail_ = 0;
        count_.store(0, std::memory_order_release);
    }
    
    /**
     * Get buffer utilization percentage (0-100)
     */
    uint8_t utilization() const {
        return static_cast<uint8_t>((count() * 100) / Size);
    }

private:
    std::array<T, Size> buffer_;
    size_t head_;
    size_t tail_;
    std::atomic<size_t> count_;
};

// ============================================================================
// CAN-specific ring buffer manager
// ============================================================================

template<size_t TxSize, size_t RxSize>
class CanBufferManager {
public:
    CanBufferManager() : tx_overflows_(0), rx_overflows_(0) {}
    
    // Transmit operations
    bool queue_tx(const CanMessage& msg) {
        if (!tx_buffer_.push(msg)) {
            tx_overflows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }
    
    bool queue_tx(CanMessage&& msg) {
        if (!tx_buffer_.push(std::move(msg))) {
            tx_overflows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }
    
    std::optional<CanMessage> get_tx() {
        return tx_buffer_.pop();
    }
    
    std::optional<CanMessage> peek_tx() const {
        return tx_buffer_.peek();
    }
    
    // Receive operations
    bool queue_rx(const CanMessage& msg) {
        if (!rx_buffer_.push(msg)) {
            rx_overflows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }
    
    std::optional<CanMessage> get_rx() {
        return rx_buffer_.pop();
    }
    
    // Status queries
    bool has_tx_data() const { return !tx_buffer_.is_empty(); }
    bool has_rx_data() const { return !rx_buffer_.is_empty(); }
    bool tx_full() const { return tx_buffer_.is_full(); }
    bool rx_full() const { return rx_buffer_.is_full(); }
    
    size_t tx_count() const { return tx_buffer_.count(); }
    size_t rx_count() const { return rx_buffer_.count(); }
    
    uint32_t tx_overflow_count() const { 
        return tx_overflows_.load(std::memory_order_relaxed); 
    }
    uint32_t rx_overflow_count() const { 
        return rx_overflows_.load(std::memory_order_relaxed); 
    }
    
    // Buffer statistics
    struct BufferStats {
        size_t tx_count;
        size_t tx_capacity;
        uint8_t tx_utilization;
        uint32_t tx_overflows;
        
        size_t rx_count;
        size_t rx_capacity;
        uint8_t rx_utilization;
        uint32_t rx_overflows;
    };
    
    BufferStats get_stats() const {
        return {
            tx_buffer_.count(),
            TxSize,
            tx_buffer_.utilization(),
            tx_overflow_count(),
            
            rx_buffer_.count(),
            RxSize,
            rx_buffer_.utilization(),
            rx_overflow_count()
        };
    }
    
    void clear_buffers() {
        tx_buffer_.clear();
        rx_buffer_.clear();
    }
    
    void reset_overflow_counters() {
        tx_overflows_.store(0, std::memory_order_relaxed);
        rx_overflows_.store(0, std::memory_order_relaxed);
    }

private:
    RingBuffer<CanMessage, TxSize> tx_buffer_;
    RingBuffer<CanMessage, RxSize> rx_buffer_;
    std::atomic<uint32_t> tx_overflows_;
    std::atomic<uint32_t> rx_overflows_;
};

// ============================================================================
// Usage examples
// ============================================================================

// Global buffer manager instance
CanBufferManager<32, 64> can_buffers;

/**
 * Send a CAN message (application level)
 */
bool send_can_message(uint32_t id, const uint8_t* data, uint8_t len) {
    CanMessage msg;
    msg.id = id;
    msg.dlc = len;
    std::copy(data, data + len, msg.data.begin());
    msg.is_extended = false;
    msg.is_rtr = false;
    
    bool success = can_buffers.queue_tx(std::move(msg));
    
    if (success) {
        // Enable TX interrupt to start transmission
        enable_can_tx_interrupt();
    }
    
    return success;
}

/**
 * CAN TX interrupt handler (C++ style)
 */
extern "C" void CAN_TX_IRQHandler(void) {
    auto msg = can_buffers.get_tx();
    
    if (msg.has_value()) {
        // Load message into hardware
        load_can_hardware(*msg);
    } else {
        // No more messages - disable interrupt
        disable_can_tx_interrupt();
    }
}

/**
 * CAN RX interrupt handler
 */
extern "C" void CAN_RX_IRQHandler(void) {
    CanMessage msg;
    
    // Read from hardware
    read_can_hardware(msg);
    
    // Queue for processing
    can_buffers.queue_rx(std::move(msg));
}

/**
 * Process received messages with callback pattern
 */
template<typename Callback>
void process_rx_messages(Callback&& callback) {
    while (auto msg = can_buffers.get_rx()) {
        callback(*msg);
    }
}

/**
 * Example: Message processing with lambda
 */
void main_loop() {
    process_rx_messages([](const CanMessage& msg) {
        switch (msg.id) {
            case 0x100:
                handle_sensor_data(msg);
                break;
            case 0x200:
                handle_command(msg);
                break;
            default:
                break;
        }
    });
}

/**
 * Example: Priority message sending with overflow handling
 */
class PriorityCanSender {
public:
    enum class Priority { Low, Normal, High, Critical };
    
    bool send(uint32_t id, const uint8_t* data, uint8_t len, Priority priority) {
        CanMessage msg;
        msg.id = id;
        msg.dlc = len;
        std::copy(data, data + len, msg.data.begin());
        
        // Attempt to queue
        if (can_buffers.queue_tx(msg)) {
            return true;
        }
        
        // Handle overflow based on priority
        if (priority >= Priority::High) {
            // For high-priority messages, try to discard a low-priority message
            // (This would require a more sophisticated priority queue)
            return retry_with_discard(msg);
        }
        
        return false;
    }
    
private:
    bool retry_with_discard(const CanMessage& msg) {
        // Implementation would require priority-aware queue
        return false;
    }
};

/**
 * Example: Buffer monitoring and diagnostics
 */
class CanDiagnostics {
public:
    void periodic_check() {
        auto stats = can_buffers.get_stats();
        
        // Check for high utilization
        if (stats.tx_utilization > 80) {
            log_warning("TX buffer high utilization: {}%", stats.tx_utilization);
        }
        
        if (stats.rx_utilization > 80) {
            log_warning("RX buffer high utilization: {}%", stats.rx_utilization);
            log_warning("Application may not be processing messages fast enough");
        }
        
        // Check for overflows
        if (stats.tx_overflows > last_tx_overflows_) {
            log_error("TX buffer overflows: {} new, {} total", 
                     stats.tx_overflows - last_tx_overflows_,
                     stats.tx_overflows);
            last_tx_overflows_ = stats.tx_overflows;
        }
        
        if (stats.rx_overflows > last_rx_overflows_) {
            log_error("RX buffer overflows: {} new, {} total",
                     stats.rx_overflows - last_rx_overflows_,
                     stats.rx_overflows);
            last_rx_overflows_ = stats.rx_overflows;
        }
    }
    
private:
    uint32_t last_tx_overflows_ = 0;
    uint32_t last_rx_overflows_ = 0;
};