#include <iostream>
#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <cstring>
#include <algorithm>

// ============================================================================
// CIRCULAR BUFFER (Thread-Safe)
// ============================================================================

template<typename T>
class CircularBuffer {
private:
    std::vector<T> buffer_;
    size_t head_;
    size_t tail_;
    size_t size_;
    size_t capacity_;
    mutable std::mutex mutex_;

public:
    explicit CircularBuffer(size_t capacity) 
        : buffer_(capacity), head_(0), tail_(0), size_(0), capacity_(capacity) {}

    bool write(const T* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (count > available_write()) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            buffer_[head_] = data[i];
            head_ = (head_ + 1) % capacity_;
            ++size_;
        }

        return true;
    }

    size_t read(T* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t to_read = std::min(count, size_);

        for (size_t i = 0; i < to_read; ++i) {
            data[i] = buffer_[tail_];
            tail_ = (tail_ + 1) % capacity_;
            --size_;
        }

        return to_read;
    }

    size_t peek(T* data, size_t count) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t to_peek = std::min(count, size_);
        size_t pos = tail_;

        for (size_t i = 0; i < to_peek; ++i) {
            data[i] = buffer_[pos];
            pos = (pos + 1) % capacity_;
        }

        return to_peek;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = tail_ = size_ = 0;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    size_t available_write() const {
        return capacity_ - size_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == capacity_;
    }
};

// ============================================================================
// DYNAMIC BUFFER WITH SMART GROWTH
// ============================================================================

class DynamicBuffer {
private:
    std::vector<uint8_t> data_;
    size_t read_pos_;
    double growth_factor_;

public:
    explicit DynamicBuffer(size_t initial_capacity = 4096, double growth_factor = 1.5)
        : data_(), read_pos_(0), growth_factor_(growth_factor) {
        data_.reserve(initial_capacity);
    }

    void append(const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        data_.insert(data_.end(), bytes, bytes + size);
    }

    void append(const std::vector<uint8_t>& data) {
        data_.insert(data_.end(), data.begin(), data.end());
    }

    template<typename T>
    void append(const T& value) {
        append(&value, sizeof(T));
    }

    size_t consume(size_t count) {
        size_t available = data_.size() - read_pos_;
        size_t to_consume = std::min(count, available);
        
        read_pos_ += to_consume;

        // Compact buffer if we've consumed more than half
        if (read_pos_ > data_.size() / 2 && read_pos_ > 0) {
            data_.erase(data_.begin(), data_.begin() + read_pos_);
            read_pos_ = 0;
        }

        return to_consume;
    }

    const uint8_t* data() const {
        return data_.data() + read_pos_;
    }

    uint8_t* data() {
        return data_.data() + read_pos_;
    }

    size_t size() const {
        return data_.size() - read_pos_;
    }

    void clear() {
        data_.clear();
        read_pos_ = 0;
    }

    void reserve(size_t capacity) {
        data_.reserve(capacity);
    }

    std::vector<uint8_t> extract(size_t count) {
        size_t available = std::min(count, size());
        std::vector<uint8_t> result(data_.begin() + read_pos_, 
                                    data_.begin() + read_pos_ + available);
        consume(available);
        return result;
    }
};

// ============================================================================
// BUFFER POOL WITH SHARED POINTERS
// ============================================================================

class BufferPool {
private:
    struct Buffer {
        std::vector<uint8_t> data;
        explicit Buffer(size_t size) : data(size) {}
    };

    std::queue<std::shared_ptr<Buffer>> pool_;
    size_t buffer_size_;
    size_t max_pool_size_;
    mutable std::mutex mutex_;

public:
    BufferPool(size_t buffer_size, size_t initial_count, size_t max_pool_size = 100)
        : buffer_size_(buffer_size), max_pool_size_(max_pool_size) {
        
        for (size_t i = 0; i < initial_count; ++i) {
            pool_.push(std::make_shared<Buffer>(buffer_size_));
        }
    }

    std::shared_ptr<std::vector<uint8_t>> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!pool_.empty()) {
            auto buffer = pool_.front();
            pool_.pop();
            return std::shared_ptr<std::vector<uint8_t>>(
                buffer, &buffer->data
            );
        }

        // Create new buffer if pool is empty
        auto buffer = std::make_shared<Buffer>(buffer_size_);
        return std::shared_ptr<std::vector<uint8_t>>(
            buffer, &buffer->data
        );
    }

    void release(std::shared_ptr<std::vector<uint8_t>> buffer) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (pool_.size() < max_pool_size_) {
            // Clear buffer data before returning to pool
            buffer->clear();
            buffer->reserve(buffer_size_);
            
            // Re-wrap in Buffer struct
            auto buf = std::make_shared<Buffer>(buffer_size_);
            buf->data = std::move(*buffer);
            pool_.push(buf);
        }
        // Otherwise, let it be destroyed
    }

    size_t pool_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }
};

// ============================================================================
// WEBSOCKET FRAME BUFFER (Practical Example)
// ============================================================================

class WebSocketFrameBuffer {
private:
    DynamicBuffer recv_buffer_;
    CircularBuffer<uint8_t> send_buffer_;
    
    static constexpr size_t MAX_FRAME_SIZE = 65536;
    static constexpr size_t SEND_BUFFER_SIZE = 131072;

public:
    WebSocketFrameBuffer() 
        : recv_buffer_(4096), send_buffer_(SEND_BUFFER_SIZE) {}

    // Add received data
    void add_received_data(const uint8_t* data, size_t size) {
        recv_buffer_.append(data, size);
    }

    // Try to extract a complete frame
    bool try_extract_frame(std::vector<uint8_t>& frame) {
        if (recv_buffer_.size() < 2) {
            return false; // Not enough data for header
        }

        const uint8_t* data = recv_buffer_.data();
        
        // Parse WebSocket frame header (simplified)
        bool fin = (data[0] & 0x80) != 0;
        uint8_t opcode = data[0] & 0x0F;
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payload_len = data[1] & 0x7F;

        size_t header_size = 2;
        size_t offset = 2;

        // Extended payload length
        if (payload_len == 126) {
            if (recv_buffer_.size() < 4) return false;
            payload_len = (data[2] << 8) | data[3];
            header_size = 4;
            offset = 4;
        } else if (payload_len == 127) {
            if (recv_buffer_.size() < 10) return false;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[2 + i];
            }
            header_size = 10;
            offset = 10;
        }

        // Masking key
        uint8_t mask_key[4] = {0};
        if (masked) {
            if (recv_buffer_.size() < offset + 4) return false;
            std::memcpy(mask_key, data + offset, 4);
            header_size += 4;
            offset += 4;
        }

        // Check if we have complete frame
        size_t total_size = header_size + payload_len;
        if (recv_buffer_.size() < total_size) {
            return false;
        }

        // Extract payload
        frame.resize(payload_len);
        std::memcpy(frame.data(), data + offset, payload_len);

        // Unmask if needed
        if (masked) {
            for (size_t i = 0; i < payload_len; ++i) {
                frame[i] ^= mask_key[i % 4];
            }
        }

        // Consume processed data
        recv_buffer_.consume(total_size);

        return true;
    }

    // Queue data for sending
    bool queue_send_data(const uint8_t* data, size_t size) {
        return send_buffer_.write(data, size);
    }

    // Get data to send
    size_t get_send_data(uint8_t* buffer, size_t max_size) {
        return send_buffer_.read(buffer, max_size);
    }

    size_t pending_send_size() const {
        return send_buffer_.size();
    }
};

// ============================================================================
// DEMONSTRATION
// ============================================================================

int main() {
    std::cout << "=== WebSocket Buffer Management (C++) ===\n\n";

    // Test circular buffer
    std::cout << "1. Circular Buffer Test:\n";
    CircularBuffer<uint8_t> cb(64);
    
    const char* msg = "Hello, WebSocket!";
    cb.write(reinterpret_cast<const uint8_t*>(msg), std::strlen(msg));
    std::cout << "   Buffer size: " << cb.size() << "/64\n";
    
    uint8_t read_buf[10];
    size_t read = cb.read(read_buf, 5);
    std::cout << "   Read: " << read << " bytes\n";
    std::cout << "   Remaining: " << cb.size() << " bytes\n\n";

    // Test dynamic buffer
    std::cout << "2. Dynamic Buffer Test:\n";
    DynamicBuffer db(16);
    
    const char* msg2 = "This message will grow the buffer!";
    db.append(msg2, std::strlen(msg2));
    std::cout << "   Buffer size: " << db.size() << " bytes\n";
    
    db.consume(10);
    std::cout << "   After consuming 10 bytes: " << db.size() << " bytes\n\n";

    // Test buffer pool
    std::cout << "3. Buffer Pool Test:\n";
    BufferPool pool(1024, 4);
    std::cout << "   Initial pool size: " << pool.pool_size() << "\n";
    
    auto buf1 = pool.acquire();
    auto buf2 = pool.acquire();
    std::cout << "   After acquiring 2 buffers: " << pool.pool_size() << "\n";
    
    pool.release(buf1);
    pool.release(buf2);
    std::cout << "   After releasing 2 buffers: " << pool.pool_size() << "\n\n";

    // Test WebSocket frame buffer
    std::cout << "4. WebSocket Frame Buffer Test:\n";
    WebSocketFrameBuffer ws_buffer;
    
    // Simulate received data (simple unmasked text frame)
    uint8_t frame_data[] = {0x81, 0x05, 'H', 'e', 'l', 'l', 'o'};
    ws_buffer.add_received_data(frame_data, sizeof(frame_data));
    
    std::vector<uint8_t> extracted_frame;
    if (ws_buffer.try_extract_frame(extracted_frame)) {
        std::cout << "   Extracted frame: ";
        std::cout.write(reinterpret_cast<char*>(extracted_frame.data()), 
                       extracted_frame.size());
        std::cout << "\n";
    }

    std::cout << "\n=== Demo Complete ===\n";
    return 0;
}