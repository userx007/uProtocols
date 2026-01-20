#include <vector>
#include <memory>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <algorithm>

// Abstract SPI interface
class ISpiDevice {
public:
    virtual ~ISpiDevice() = default;
    virtual void transfer(const std::vector<uint8_t>& tx_data, 
                         std::vector<uint8_t>& rx_data) = 0;
    virtual void chipSelect(bool active) = 0;
};

// Batch transfer accumulator with RAII
class SpiBatch {
private:
    std::vector<uint8_t> tx_buffer_;
    std::vector<uint8_t> rx_buffer_;
    ISpiDevice* device_;
    size_t max_size_;
    bool auto_execute_;
    
public:
    SpiBatch(ISpiDevice* device, size_t max_size = 1024, bool auto_execute = false)
        : device_(device), max_size_(max_size), auto_execute_(auto_execute) {
        tx_buffer_.reserve(max_size);
        rx_buffer_.reserve(max_size);
    }
    
    // RAII: Execute on destruction if auto-execute enabled
    ~SpiBatch() {
        if (auto_execute_ && !tx_buffer_.empty()) {
            execute();
        }
    }
    
    // Add data to batch
    SpiBatch& add(const std::vector<uint8_t>& data) {
        if (tx_buffer_.size() + data.size() > max_size_) {
            throw std::overflow_error("Batch buffer overflow");
        }
        
        tx_buffer_.insert(tx_buffer_.end(), data.begin(), data.end());
        return *this; // Allow chaining
    }
    
    // Add single byte
    SpiBatch& add(uint8_t byte) {
        if (tx_buffer_.size() >= max_size_) {
            throw std::overflow_error("Batch buffer overflow");
        }
        
        tx_buffer_.push_back(byte);
        return *this;
    }
    
    // Execute the batch
    void execute() {
        if (tx_buffer_.empty()) return;
        
        rx_buffer_.resize(tx_buffer_.size());
        
        device_->chipSelect(true);
        device_->transfer(tx_buffer_, rx_buffer_);
        device_->chipSelect(false);
        
        tx_buffer_.clear();
    }
    
    // Get received data
    const std::vector<uint8_t>& rxData() const { return rx_buffer_; }
    
    // Clear buffers
    void clear() {
        tx_buffer_.clear();
        rx_buffer_.clear();
    }
    
    size_t size() const { return tx_buffer_.size(); }
    bool empty() const { return tx_buffer_.empty(); }
};

// Auto-flushing batch manager
class SpiBatchManager {
private:
    std::unique_ptr<SpiBatch> batch_;
    size_t flush_threshold_;
    std::function<void(const std::vector<uint8_t>&)> on_flush_;
    
public:
    SpiBatchManager(ISpiDevice* device, size_t max_size, size_t flush_threshold)
        : batch_(std::make_unique<SpiBatch>(device, max_size)),
          flush_threshold_(flush_threshold) {}
    
    void setFlushCallback(std::function<void(const std::vector<uint8_t>&)> cb) {
        on_flush_ = cb;
    }
    
    void add(const std::vector<uint8_t>& data) {
        if (batch_->size() + data.size() >= flush_threshold_) {
            flush();
        }
        batch_->add(data);
    }
    
    void flush() {
        if (batch_->empty()) return;
        
        if (on_flush_) {
            on_flush_(batch_->rxData());
        }
        
        batch_->execute();
    }
    
    ~SpiBatchManager() {
        flush(); // Ensure all data is sent
    }
};

// Example: Register batch operations
class RegisterBatch {
private:
    SpiBatch batch_;
    std::vector<uint8_t> read_addresses_;
    
public:
    RegisterBatch(ISpiDevice* device) : batch_(device, 256) {}
    
    // Queue a register read
    void queueRead(uint8_t reg_addr) {
        batch_.add(reg_addr | 0x80); // Read bit
        batch_.add(0x00);            // Dummy byte
        read_addresses_.push_back(reg_addr);
    }
    
    // Queue a register write
    void queueWrite(uint8_t reg_addr, uint8_t value) {
        batch_.add(reg_addr & 0x7F); // Write (clear read bit)
        batch_.add(value);
    }
    
    // Execute and parse results
    std::vector<std::pair<uint8_t, uint8_t>> execute() {
        batch_.execute();
        
        std::vector<std::pair<uint8_t, uint8_t>> results;
        const auto& rx = batch_.rxData();
        
        for (size_t i = 0; i < read_addresses_.size(); i++) {
            results.push_back({read_addresses_[i], rx[i * 2 + 1]});
        }
        
        read_addresses_.clear();
        return results;
    }
};

// Example: Display update with progressive batching
class DisplayBatcher {
private:
    ISpiDevice* device_;
    static constexpr size_t CHUNK_SIZE = 512;
    
public:
    DisplayBatcher(ISpiDevice* device) : device_(device) {}
    
    void updateFramebuffer(const std::vector<uint8_t>& framebuffer) {
        size_t offset = 0;
        
        while (offset < framebuffer.size()) {
            SpiBatch batch(device_, CHUNK_SIZE + 1, true); // Auto-execute
            
            // Add command byte
            batch.add(0x40); // Write to RAM command
            
            // Add pixel data chunk
            size_t chunk_len = std::min(CHUNK_SIZE, framebuffer.size() - offset);
            batch.add(std::vector<uint8_t>(
                framebuffer.begin() + offset,
                framebuffer.begin() + offset + chunk_len
            ));
            
            offset += chunk_len;
            // Batch auto-executes on destruction
        }
    }
};

// Example: Sensor data collection with batching
template<typename T>
class SensorBatcher {
private:
    ISpiDevice* device_;
    SpiBatchManager manager_;
    std::vector<T> collected_data_;
    
public:
    SensorBatcher(ISpiDevice* device, size_t batch_size = 512)
        : device_(device), manager_(device, batch_size, batch_size / 2) {
        
        manager_.setFlushCallback([this](const auto& rx_data) {
            // Parse received sensor data
            for (size_t i = 0; i < rx_data.size(); i += sizeof(T)) {
                T value;
                std::memcpy(&value, &rx_data[i], sizeof(T));
                collected_data_.push_back(value);
            }
        });
    }
    
    void queueSample(uint8_t sensor_id) {
        std::vector<uint8_t> cmd = {0xA0, sensor_id}; // Read sensor command
        manager_.add(cmd);
    }
    
    void finalize() {
        manager_.flush();
    }
    
    const std::vector<T>& getData() const { return collected_data_; }
};

// Usage example
void demonstrateBatching(ISpiDevice* spi) {
    std::cout << "=== Register Batch Operations ===" << std::endl;
    
    RegisterBatch reg_batch(spi);
    
    // Queue multiple operations
    reg_batch.queueRead(0x00);
    reg_batch.queueRead(0x01);
    reg_batch.queueWrite(0x10, 0x55);
    reg_batch.queueRead(0x02);
    
    // Execute as single transaction
    auto results = reg_batch.execute();
    
    for (const auto& [addr, value] : results) {
        std::cout << "Reg 0x" << std::hex << (int)addr 
                  << " = 0x" << (int)value << std::endl;
    }
    
    std::cout << "\n=== Sensor Data Collection ===" << std::endl;
    
    SensorBatcher<uint16_t> sensor_batch(spi);
    
    for (int i = 0; i < 100; i++) {
        sensor_batch.queueSample(i % 4); // 4 sensors, round-robin
    }
    
    sensor_batch.finalize();
    
    std::cout << "Collected " << sensor_batch.getData().size() 
              << " samples" << std::endl;
}