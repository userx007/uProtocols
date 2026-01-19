#include <iostream>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <array>
#include <cstring>

// Generic object pool template
template<typename T>
class ObjectPool {
private:
    struct PooledObject {
        alignas(T) unsigned char storage[sizeof(T)];
        bool in_use;
        
        T* get() { return reinterpret_cast<T*>(storage); }
    };
    
    std::vector<std::unique_ptr<PooledObject>> objects_;
    std::queue<PooledObject*> available_;
    std::mutex mutex_;
    size_t initial_size_;
    size_t max_size_;
    size_t allocated_count_;
    
public:
    ObjectPool(size_t initial_size = 100, size_t max_size = 1000) 
        : initial_size_(initial_size), max_size_(max_size), allocated_count_(0) {
        
        // Pre-allocate initial pool
        for (size_t i = 0; i < initial_size_; i++) {
            auto obj = std::make_unique<PooledObject>();
            obj->in_use = false;
            available_.push(obj.get());
            objects_.push_back(std::move(obj));
        }
    }
    
    ~ObjectPool() {
        // Destroy all objects
        for (auto& obj : objects_) {
            if (obj->in_use) {
                obj->get()->~T();
            }
        }
    }
    
    // Acquire object from pool
    template<typename... Args>
    T* acquire(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PooledObject* obj = nullptr;
        
        if (!available_.empty()) {
            // Reuse from pool
            obj = available_.front();
            available_.pop();
        } else if (objects_.size() < max_size_) {
            // Expand pool
            auto new_obj = std::make_unique<PooledObject>();
            obj = new_obj.get();
            objects_.push_back(std::move(new_obj));
        } else {
            // Pool exhausted, return nullptr
            return nullptr;
        }
        
        obj->in_use = true;
        allocated_count_++;
        
        // Construct object in-place
        new (obj->get()) T(std::forward<Args>(args)...);
        
        return obj->get();
    }
    
    // Release object back to pool
    void release(T* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Find the pooled object
        for (auto& obj : objects_) {
            if (obj->get() == ptr) {
                if (obj->in_use) {
                    // Explicitly destroy the object
                    ptr->~T();
                    obj->in_use = false;
                    available_.push(obj.get());
                }
                return;
            }
        }
    }
    
    size_t size() const { return objects_.size(); }
    size_t available() const { return available_.size(); }
    size_t allocated() const { return allocated_count_; }
};

// Custom deleter for pooled objects
template<typename T>
class PoolDeleter {
private:
    ObjectPool<T>* pool_;
    
public:
    PoolDeleter(ObjectPool<T>* pool = nullptr) : pool_(pool) {}
    
    void operator()(T* ptr) {
        if (pool_) {
            pool_->release(ptr);
        } else {
            delete ptr;
        }
    }
};

// WebSocket frame class
class WSFrame {
public:
    enum class Opcode : uint8_t {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA
    };
    
private:
    std::vector<uint8_t> payload_;
    Opcode opcode_;
    bool fin_;
    
public:
    WSFrame(Opcode opcode = Opcode::TEXT, bool fin = true) 
        : opcode_(opcode), fin_(fin) {}
    
    void set_payload(const uint8_t* data, size_t len) {
        payload_.assign(data, data + len);
    }
    
    void set_payload(const std::string& data) {
        payload_.assign(data.begin(), data.end());
    }
    
    const std::vector<uint8_t>& payload() const { return payload_; }
    Opcode opcode() const { return opcode_; }
    bool fin() const { return fin_; }
    
    void clear() {
        payload_.clear();
        opcode_ = Opcode::TEXT;
        fin_ = true;
    }
};

// Memory arena for small allocations
class MemoryArena {
private:
    static constexpr size_t BLOCK_SIZE = 64 * 1024; // 64KB blocks
    
    struct Block {
        std::unique_ptr<uint8_t[]> data;
        size_t used;
        
        Block() : data(new uint8_t[BLOCK_SIZE]), used(0) {}
    };
    
    std::vector<std::unique_ptr<Block>> blocks_;
    Block* current_block_;
    
public:
    MemoryArena() {
        blocks_.push_back(std::make_unique<Block>());
        current_block_ = blocks_.back().get();
    }
    
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        // Align the current position
        size_t padding = (alignment - (current_block_->used % alignment)) % alignment;
        size_t total = size + padding;
        
        if (current_block_->used + total > BLOCK_SIZE) {
            // Need new block
            blocks_.push_back(std::make_unique<Block>());
            current_block_ = blocks_.back().get();
            padding = 0;
            total = size;
        }
        
        void* ptr = current_block_->data.get() + current_block_->used + padding;
        current_block_->used += total;
        return ptr;
    }
    
    void reset() {
        for (auto& block : blocks_) {
            block->used = 0;
        }
        current_block_ = blocks_[0].get();
    }
    
    size_t total_allocated() const {
        return blocks_.size() * BLOCK_SIZE;
    }
};

// WebSocket connection with pooled resources
class WSConnection {
private:
    ObjectPool<WSFrame>* frame_pool_;
    MemoryArena arena_;
    uint64_t connection_id_;
    
public:
    WSConnection(ObjectPool<WSFrame>* pool, uint64_t id) 
        : frame_pool_(pool), connection_id_(id) {}
    
    std::unique_ptr<WSFrame, PoolDeleter<WSFrame>> create_frame(
        WSFrame::Opcode opcode = WSFrame::Opcode::TEXT) {
        
        WSFrame* frame = frame_pool_->acquire(opcode);
        return std::unique_ptr<WSFrame, PoolDeleter<WSFrame>>(
            frame, PoolDeleter<WSFrame>(frame_pool_));
    }
    
    void* allocate_temp(size_t size) {
        return arena_.allocate(size);
    }
    
    void reset_temp_memory() {
        arena_.reset();
    }
    
    uint64_t id() const { return connection_id_; }
};

// Example usage
int main() {
    // Create frame pool
    ObjectPool<WSFrame> frame_pool(50, 500);
    
    std::cout << "Initial pool size: " << frame_pool.size() 
              << ", available: " << frame_pool.available() << std::endl;
    
    // Create connections
    std::vector<std::unique_ptr<WSConnection>> connections;
    for (int i = 0; i < 10; i++) {
        connections.push_back(
            std::make_unique<WSConnection>(&frame_pool, i));
    }
    
    // Simulate message processing
    std::vector<std::unique_ptr<WSFrame, PoolDeleter<WSFrame>>> active_frames;
    
    for (int round = 0; round < 3; round++) {
        std::cout << "\n--- Round " << round + 1 << " ---" << std::endl;
        
        // Each connection sends messages
        for (auto& conn : connections) {
            auto frame = conn->create_frame(WSFrame::Opcode::TEXT);
            if (frame) {
                std::string msg = "Message from connection " + 
                                std::to_string(conn->id());
                frame->set_payload(msg);
                active_frames.push_back(std::move(frame));
            }
        }
        
        std::cout << "Active frames: " << active_frames.size() << std::endl;
        std::cout << "Pool available: " << frame_pool.available() << std::endl;
        std::cout << "Total allocated: " << frame_pool.allocated() << std::endl;
        
        // Clear half the frames (simulating processing)
        if (round > 0) {
            active_frames.erase(
                active_frames.begin(), 
                active_frames.begin() + active_frames.size() / 2);
        }
    }
    
    // Cleanup
    active_frames.clear();
    std::cout << "\nAfter cleanup:" << std::endl;
    std::cout << "Pool available: " << frame_pool.available() << std::endl;
    
    return 0;
}