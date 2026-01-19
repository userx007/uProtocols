#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <memory>
#include <stdexcept>

// Buffer chain node for zero-copy operations
struct BufferNode {
    const uint8_t* data;
    size_t length;
    bool owned;  // Whether this node owns the memory
    
    BufferNode(const uint8_t* d, size_t len, bool own = false)
        : data(d), length(len), owned(own) {}
    
    ~BufferNode() {
        if (owned && data) {
            delete[] data;
        }
    }
};

class ZeroCopyWebSocketFrame {
private:
    std::vector<std::unique_ptr<BufferNode>> chain;
    size_t total_size;
    
public:
    ZeroCopyWebSocketFrame() : total_size(0) {}
    
    // Add a buffer to the chain without copying
    void addBuffer(const uint8_t* data, size_t length, bool owned = false) {
        chain.push_back(std::make_unique<BufferNode>(data, length, owned));
        total_size += length;
    }
    
    // Create WebSocket frame header
    void addHeader(uint8_t opcode, size_t payload_length, bool fin = true) {
        uint8_t* header = new uint8_t[10];  // Max header size
        size_t header_len = 0;
        
        // First byte: FIN, RSV, opcode
        header[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
        header_len++;
        
        // Payload length
        if (payload_length < 126) {
            header[1] = static_cast<uint8_t>(payload_length);
            header_len++;
        } else if (payload_length < 65536) {
            header[1] = 126;
            header[2] = (payload_length >> 8) & 0xFF;
            header[3] = payload_length & 0xFF;
            header_len += 3;
        } else {
            header[1] = 127;
            for (int i = 7; i >= 0; --i) {
                header[2 + i] = (payload_length >> (8 * (7 - i))) & 0xFF;
            }
            header_len += 9;
        }
        
        // Add to chain (owned memory)
        chain.insert(chain.begin(), 
                    std::make_unique<BufferNode>(header, header_len, true));
        total_size += header_len;
    }
    
    // Send using scatter-gather I/O (writev)
    ssize_t sendScatterGather(int socket_fd) {
        // Prepare iovec structures for writev
        std::vector<struct iovec> iov(chain.size());
        
        for (size_t i = 0; i < chain.size(); ++i) {
            iov[i].iov_base = const_cast<uint8_t*>(chain[i]->data);
            iov[i].iov_len = chain[i]->length;
        }
        
        // Single system call to send all buffers
        ssize_t sent = writev(socket_fd, iov.data(), iov.size());
        
        if (sent < 0) {
            throw std::runtime_error("writev failed");
        }
        
        return sent;
    }
    
    // Receive using scatter-gather I/O (readv)
    static ssize_t receiveScatterGather(int socket_fd, 
                                        std::vector<std::pair<uint8_t*, size_t>>& buffers) {
        std::vector<struct iovec> iov(buffers.size());
        
        for (size_t i = 0; i < buffers.size(); ++i) {
            iov[i].iov_base = buffers[i].first;
            iov[i].iov_len = buffers[i].second;
        }
        
        ssize_t received = readv(socket_fd, iov.data(), iov.size());
        
        if (received < 0) {
            throw std::runtime_error("readv failed");
        }
        
        return received;
    }
    
    size_t getTotalSize() const { return total_size; }
};

// Example: Memory-mapped buffer pool
class BufferPool {
private:
    std::vector<uint8_t*> available_buffers;
    size_t buffer_size;
    static const size_t POOL_SIZE = 1024;
    
public:
    BufferPool(size_t buf_size) : buffer_size(buf_size) {
        // Pre-allocate buffers
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            available_buffers.push_back(new uint8_t[buffer_size]);
        }
    }
    
    ~BufferPool() {
        for (auto buf : available_buffers) {
            delete[] buf;
        }
    }
    
    uint8_t* acquire() {
        if (available_buffers.empty()) {
            return new uint8_t[buffer_size];
        }
        uint8_t* buf = available_buffers.back();
        available_buffers.pop_back();
        return buf;
    }
    
    void release(uint8_t* buf) {
        if (available_buffers.size() < POOL_SIZE) {
            available_buffers.push_back(buf);
        } else {
            delete[] buf;
        }
    }
};

// Usage example
int main() {
    // Simulated socket (in real code, use actual socket)
    int sockfd = 1;
    
    // Create a zero-copy frame
    ZeroCopyWebSocketFrame frame;
    
    // Payload data (not copied, just referenced)
    const char* payload = "Hello, WebSocket with zero-copy!";
    size_t payload_len = strlen(payload);
    
    // Add header
    frame.addHeader(0x01, payload_len, true);  // Text frame, FIN bit set
    
    // Add payload without copying
    frame.addBuffer(reinterpret_cast<const uint8_t*>(payload), 
                    payload_len, false);
    
    // Send using scatter-gather I/O
    // frame.sendScatterGather(sockfd);
    
    // Example: Receiving into multiple buffers
    uint8_t header_buf[14];
    uint8_t payload_buf[1024];
    
    std::vector<std::pair<uint8_t*, size_t>> recv_buffers = {
        {header_buf, sizeof(header_buf)},
        {payload_buf, sizeof(payload_buf)}
    };
    
    // ssize_t received = ZeroCopyWebSocketFrame::receiveScatterGather(
    //     sockfd, recv_buffers);
    
    return 0;
}