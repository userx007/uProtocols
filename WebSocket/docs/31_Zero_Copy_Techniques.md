# Zero-Copy Techniques in WebSocket Programming

Zero-copy techniques are optimization strategies that minimize or eliminate unnecessary data copying operations between different memory buffers during I/O operations. In WebSocket implementations, these techniques are crucial for achieving high performance, especially when handling large message payloads or maintaining numerous concurrent connections.

## Core Concepts

**The Problem:**
Traditional I/O operations often involve multiple data copies:
1. Data copied from network interface to kernel buffer
2. Copied from kernel buffer to user-space buffer
3. Copied from user buffer to application buffer
4. Additional copies during frame assembly/disassembly

Each copy operation consumes CPU cycles and memory bandwidth, creating performance bottlenecks.

**The Solution:**
Zero-copy techniques reduce these operations by:
- Using direct memory mapping
- Scatter-gather I/O operations
- Buffer chaining and reference counting
- Memory view abstractions

## Scatter-Gather I/O

Scatter-gather I/O allows reading into or writing from multiple non-contiguous buffers in a single system call. For WebSockets, this is particularly useful when assembling frames that consist of headers, payload, and potentially masking data.

## Buffer Chaining

Buffer chaining maintains references to memory regions rather than copying data. When constructing a WebSocket frame, you can chain together the header buffer, payload buffer, and any additional data without allocating new memory or copying bytes.

## Code Examples

### C/C++ Implementation

```cpp
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
```

### Rust Implementation

```rust
use std::io::{self, IoSlice, IoSliceMut, Write};
use std::os::unix::io::AsRawFd;
use std::sync::Arc;

/// Buffer that can be shared without copying
#[derive(Clone)]
enum BufferRef {
    Owned(Arc<Vec<u8>>),
    Borrowed(&'static [u8]),
}

impl BufferRef {
    fn as_slice(&self) -> &[u8] {
        match self {
            BufferRef::Owned(arc) => arc.as_slice(),
            BufferRef::Borrowed(slice) => slice,
        }
    }
}

/// Zero-copy buffer chain for WebSocket frames
struct BufferChain {
    buffers: Vec<BufferRef>,
    total_size: usize,
}

impl BufferChain {
    fn new() -> Self {
        BufferChain {
            buffers: Vec::new(),
            total_size: 0,
        }
    }

    /// Add a buffer without copying
    fn add_owned(&mut self, data: Vec<u8>) {
        self.total_size += data.len();
        self.buffers.push(BufferRef::Owned(Arc::new(data)));
    }

    /// Add a static buffer by reference
    fn add_borrowed(&mut self, data: &'static [u8]) {
        self.total_size += data.len();
        self.buffers.push(BufferRef::Borrowed(data));
    }

    /// Get total size
    fn len(&self) -> usize {
        self.total_size
    }

    /// Create IoSlice array for vectored I/O
    fn as_io_slices(&self) -> Vec<IoSlice> {
        self.buffers
            .iter()
            .map(|buf| IoSlice::new(buf.as_slice()))
            .collect()
    }
}

/// WebSocket frame builder using zero-copy techniques
struct ZeroCopyWebSocketFrame {
    chain: BufferChain,
}

impl ZeroCopyWebSocketFrame {
    fn new() -> Self {
        ZeroCopyWebSocketFrame {
            chain: BufferChain::new(),
        }
    }

    /// Add WebSocket header
    fn add_header(&mut self, opcode: u8, payload_len: usize, fin: bool) {
        let mut header = Vec::new();

        // First byte: FIN, RSV, opcode
        let first_byte = if fin { 0x80 } else { 0x00 } | (opcode & 0x0F);
        header.push(first_byte);

        // Payload length encoding
        if payload_len < 126 {
            header.push(payload_len as u8);
        } else if payload_len < 65536 {
            header.push(126);
            header.extend_from_slice(&(payload_len as u16).to_be_bytes());
        } else {
            header.push(127);
            header.extend_from_slice(&(payload_len as u64).to_be_bytes());
        }

        // Insert at beginning of chain
        let old_buffers = std::mem::take(&mut self.chain.buffers);
        self.chain.total_size += header.len();
        self.chain.buffers.push(BufferRef::Owned(Arc::new(header)));
        self.chain.buffers.extend(old_buffers);
    }

    /// Add payload by moving ownership (zero-copy)
    fn add_payload_owned(&mut self, payload: Vec<u8>) {
        self.chain.add_owned(payload);
    }

    /// Add payload by reference (zero-copy for static data)
    fn add_payload_borrowed(&mut self, payload: &'static [u8]) {
        self.chain.add_borrowed(payload);
    }

    /// Send using vectored I/O (writev)
    fn send_vectored<W: Write>(&self, writer: &mut W) -> io::Result<usize> {
        let io_slices = self.chain.as_io_slices();
        writer.write_vectored(&io_slices)
    }

    /// Get total frame size
    fn len(&self) -> usize {
        self.chain.len()
    }
}

/// Receive into multiple buffers using vectored I/O
fn receive_vectored<R: std::io::Read>(
    reader: &mut R,
    buffers: &mut [IoSliceMut],
) -> io::Result<usize> {
    reader.read_vectored(buffers)
}

/// Buffer pool for reusing allocations
struct BufferPool {
    buffers: Vec<Vec<u8>>,
    buffer_size: usize,
}

impl BufferPool {
    fn new(buffer_size: usize, initial_count: usize) -> Self {
        let mut buffers = Vec::with_capacity(initial_count);
        for _ in 0..initial_count {
            buffers.push(Vec::with_capacity(buffer_size));
        }
        BufferPool {
            buffers,
            buffer_size,
        }
    }

    fn acquire(&mut self) -> Vec<u8> {
        self.buffers.pop().unwrap_or_else(|| {
            Vec::with_capacity(self.buffer_size)
        })
    }

    fn release(&mut self, mut buffer: Vec<u8>) {
        buffer.clear();
        if self.buffers.len() < 1024 {
            self.buffers.push(buffer);
        }
    }
}

/// Example: Using bytes crate for even better zero-copy support
#[cfg(feature = "bytes")]
mod with_bytes {
    use bytes::{Bytes, BytesMut, Buf, BufMut};

    pub struct BytesFrame {
        parts: Vec<Bytes>,
    }

    impl BytesFrame {
        pub fn new() -> Self {
            BytesFrame { parts: Vec::new() }
        }

        /// Add data using Bytes (reference counted, cheap clone)
        pub fn add_part(&mut self, data: Bytes) {
            self.parts.push(data);
        }

        /// Build from multiple sources without copying
        pub fn from_parts(parts: Vec<Bytes>) -> Self {
            BytesFrame { parts }
        }

        /// Get a slice of all parts (still zero-copy via Bytes)
        pub fn chain(&self) -> impl Iterator<Item = &Bytes> {
            self.parts.iter()
        }
    }
}

// Usage examples
fn main() -> io::Result<()> {
    // Example 1: Creating a frame with zero-copy
    let mut frame = ZeroCopyWebSocketFrame::new();
    
    // Payload data
    let payload = b"Hello, WebSocket!".to_vec();
    let payload_len = payload.len();
    
    // Add header
    frame.add_header(0x01, payload_len, true); // Text frame
    
    // Add payload (moves ownership, no copy)
    frame.add_payload_owned(payload);
    
    // Send using vectored I/O
    let mut stdout = io::stdout();
    // let bytes_sent = frame.send_vectored(&mut stdout)?;
    
    // Example 2: Using static data (no allocation at all)
    let mut static_frame = ZeroCopyWebSocketFrame::new();
    static STATIC_PAYLOAD: &[u8] = b"Static payload data";
    
    static_frame.add_header(0x01, STATIC_PAYLOAD.len(), true);
    static_frame.add_payload_borrowed(STATIC_PAYLOAD);
    
    // Example 3: Receiving into multiple buffers
    let mut header_buf = vec![0u8; 14];
    let mut payload_buf = vec![0u8; 1024];
    
    let mut bufs = [
        IoSliceMut::new(&mut header_buf),
        IoSliceMut::new(&mut payload_buf),
    ];
    
    // let bytes_received = receive_vectored(&mut some_reader, &mut bufs)?;
    
    // Example 4: Buffer pool
    let mut pool = BufferPool::new(4096, 10);
    let mut buffer = pool.acquire();
    
    // Use buffer...
    buffer.extend_from_slice(b"Some data");
    
    // Return to pool
    pool.release(buffer);
    
    println!("Zero-copy WebSocket frame examples completed");
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_buffer_chain() {
        let mut chain = BufferChain::new();
        chain.add_owned(vec![1, 2, 3]);
        chain.add_owned(vec![4, 5, 6]);
        
        assert_eq!(chain.len(), 6);
        assert_eq!(chain.buffers.len(), 2);
    }

    #[test]
    fn test_frame_creation() {
        let mut frame = ZeroCopyWebSocketFrame::new();
        frame.add_header(0x01, 100, true);
        
        assert!(frame.len() > 0);
    }

    #[test]
    fn test_buffer_pool() {
        let mut pool = BufferPool::new(1024, 5);
        let buf1 = pool.acquire();
        let buf2 = pool.acquire();
        
        pool.release(buf1);
        pool.release(buf2);
        
        assert_eq!(pool.buffers.len(), 2);
    }
}
```

## Detailed Explanation

### Key Techniques Demonstrated

**1. Scatter-Gather I/O (`writev`/`readv`)**

Both C++ and Rust examples use vectored I/O operations that allow the operating system to gather data from multiple buffers in a single system call. This eliminates the need to consolidate buffers into a single contiguous memory region before transmission.

In C++, this is achieved through the `writev()` and `readv()` system calls using `iovec` structures. In Rust, the standard library provides `write_vectored()` and `read_vectored()` methods that abstract these operations.

**2. Buffer Chaining**

Instead of copying data when assembling WebSocket frames, both implementations maintain a chain of buffer references. The C++ version uses a vector of unique pointers to buffer nodes, while the Rust version leverages `Arc` (atomic reference counting) for safe shared ownership.

**3. Reference vs. Ownership**

The implementations distinguish between owned and borrowed data. Owned data is managed by the buffer chain and deallocated when no longer needed. Borrowed data (like static strings) is simply referenced without any allocation or deallocation overhead.

**4. Memory Pooling**

Both examples include buffer pool implementations that reuse allocated memory across multiple operations, reducing allocation pressure and improving cache locality.

### Performance Benefits

Zero-copy techniques provide several advantages:

- **Reduced CPU Usage**: Eliminating memory copies frees up CPU cycles for other work
- **Lower Latency**: Fewer operations mean faster message processing
- **Better Cache Utilization**: Less data movement keeps caches warmer
- **Higher Throughput**: More efficient memory bandwidth usage allows handling more concurrent connections
- **Reduced Memory Pressure**: Buffer reuse through pooling decreases garbage collection overhead (in managed languages) and fragmentation

### Rust-Specific Advantages

Rust's ownership system makes zero-copy techniques particularly natural and safe. The type system prevents common errors like use-after-free or double-free that can occur in C++ implementations. The `bytes` crate (shown in the optional module) provides even more sophisticated zero-copy primitives with reference-counted byte buffers.

### Practical Considerations

While zero-copy techniques offer significant performance improvements, they also introduce complexity:

- Buffer lifetime management becomes more complex
- Error handling must account for partial writes with multiple buffers
- Some platforms have limits on the number of buffers in vectored operations
- Memory fragmentation can occur if not managed carefully with pooling

## Summary

Zero-copy techniques are essential optimizations for high-performance WebSocket implementations. By using scatter-gather I/O and buffer chaining, you can significantly reduce the overhead of frame assembly and transmission. The C++ implementation demonstrates low-level control using POSIX system calls, while the Rust implementation shows how modern language features like ownership and reference counting make these patterns safer and more ergonomic. Buffer pooling complements these techniques by reducing allocation overhead. Together, these approaches enable WebSocket servers to handle higher connection counts and message throughput while using fewer system resources.