use std::sync::{Arc, Mutex};
use std::collections::VecDeque;

// ============================================================================
// CIRCULAR BUFFER
// ============================================================================

pub struct CircularBuffer {
    buffer: Vec<u8>,
    capacity: usize,
    head: usize,
    tail: usize,
    size: usize,
}

impl CircularBuffer {
    pub fn new(capacity: usize) -> Self {
        Self {
            buffer: vec![0; capacity],
            capacity,
            head: 0,
            tail: 0,
            size: 0,
        }
    }

    pub fn write(&mut self, data: &[u8]) -> usize {
        let available = self.capacity - self.size;
        let to_write = data.len().min(available);

        for i in 0..to_write {
            self.buffer[self.head] = data[i];
            self.head = (self.head + 1) % self.capacity;
            self.size += 1;
        }

        to_write
    }

    pub fn read(&mut self, data: &mut [u8]) -> usize {
        let to_read = data.len().min(self.size);

        for i in 0..to_read {
            data[i] = self.buffer[self.tail];
            self.tail = (self.tail + 1) % self.capacity;
            self.size -= 1;
        }

        to_read
    }

    pub fn peek(&self, data: &mut [u8]) -> usize {
        let to_peek = data.len().min(self.size);
        let mut pos = self.tail;

        for i in 0..to_peek {
            data[i] = self.buffer[pos];
            pos = (pos + 1) % self.capacity;
        }

        to_peek
    }

    pub fn size(&self) -> usize {
        self.size
    }

    pub fn available(&self) -> usize {
        self.capacity - self.size
    }

    pub fn is_empty(&self) -> bool {
        self.size == 0
    }

    pub fn is_full(&self) -> bool {
        self.size == self.capacity
    }

    pub fn clear(&mut self) {
        self.head = 0;
        self.tail = 0;
        self.size = 0;
    }
}

// ============================================================================
// DYNAMIC BUFFER
// ============================================================================

pub struct DynamicBuffer {
    data: Vec<u8>,
    read_pos: usize,
    growth_factor: f64,
}

impl DynamicBuffer {
    pub fn new(initial_capacity: usize) -> Self {
        Self {
            data: Vec::with_capacity(initial_capacity),
            read_pos: 0,
            growth_factor: 1.5,
        }
    }

    pub fn with_growth_factor(initial_capacity: usize, growth_factor: f64) -> Self {
        Self {
            data: Vec::with_capacity(initial_capacity),
            read_pos: 0,
            growth_factor,
        }
    }

    pub fn append(&mut self, data: &[u8]) {
        self.data.extend_from_slice(data);
    }

    pub fn consume(&mut self, count: usize) -> usize {
        let available = self.data.len() - self.read_pos;
        let to_consume = count.min(available);

        self.read_pos += to_consume;

        // Compact buffer if we've consumed more than half
        if self.read_pos > self.data.len() / 2 && self.read_pos > 0 {
            self.data.drain(0..self.read_pos);
            self.read_pos = 0;
        }

        to_consume
    }

    pub fn data(&self) -> &[u8] {
        &self.data[self.read_pos..]
    }

    pub fn size(&self) -> usize {
        self.data.len() - self.read_pos
    }

    pub fn clear(&mut self) {
        self.data.clear();
        self.read_pos = 0;
    }

    pub fn extract(&mut self, count: usize) -> Vec<u8> {
        let available = count.min(self.size());
        let result = self.data[self.read_pos..self.read_pos + available].to_vec();
        self.consume(available);
        result
    }

    pub fn peek(&self, count: usize) -> &[u8] {
        let available = count.min(self.size());
        &self.data[self.read_pos..self.read_pos + available]
    }
}

// ============================================================================
// BUFFER POOL
// ============================================================================

pub struct BufferPool {
    pool: Arc<Mutex<VecDeque<Vec<u8>>>>,
    buffer_size: usize,
    max_pool_size: usize,
}

impl BufferPool {
    pub fn new(buffer_size: usize, initial_count: usize, max_pool_size: usize) -> Self {
        let mut pool = VecDeque::new();

        for _ in 0..initial_count {
            pool.push_back(vec![0; buffer_size]);
        }

        Self {
            pool: Arc::new(Mutex::new(pool)),
            buffer_size,
            max_pool_size,
        }
    }

    pub fn acquire(&self) -> PooledBuffer {
        let mut pool = self.pool.lock().unwrap();
        
        let buffer = pool.pop_front().unwrap_or_else(|| {
            vec![0; self.buffer_size]
        });

        PooledBuffer {
            buffer: Some(buffer),
            pool: Arc::clone(&self.pool),
            max_pool_size: self.max_pool_size,
        }
    }

    pub fn pool_size(&self) -> usize {
        self.pool.lock().unwrap().len()
    }
}

pub struct PooledBuffer {
    buffer: Option<Vec<u8>>,
    pool: Arc<Mutex<VecDeque<Vec<u8>>>>,
    max_pool_size: usize,
}

impl PooledBuffer {
    pub fn as_slice(&self) -> &[u8] {
        self.buffer.as_ref().unwrap()
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        self.buffer.as_mut().unwrap()
    }
}

impl Drop for PooledBuffer {
    fn drop(&mut self) {
        if let Some(mut buffer) = self.buffer.take() {
            let mut pool = self.pool.lock().unwrap();
            
            if pool.len() < self.max_pool_size {
                buffer.clear();
                buffer.resize(buffer.capacity(), 0);
                pool.push_back(buffer);
            }
            // Otherwise, buffer is dropped
        }
    }
}

impl std::ops::Deref for PooledBuffer {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        self.as_slice()
    }
}

impl std::ops::DerefMut for PooledBuffer {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut_slice()
    }
}

// ============================================================================
// WEBSOCKET FRAME BUFFER
// ============================================================================

pub struct WebSocketFrameBuffer {
    recv_buffer: DynamicBuffer,
    send_buffer: CircularBuffer,
}

impl WebSocketFrameBuffer {
    const MAX_FRAME_SIZE: usize = 65536;
    const SEND_BUFFER_SIZE: usize = 131072;

    pub fn new() -> Self {
        Self {
            recv_buffer: DynamicBuffer::new(4096),
            send_buffer: CircularBuffer::new(Self::SEND_BUFFER_SIZE),
        }
    }

    pub fn add_received_data(&mut self, data: &[u8]) {
        self.recv_buffer.append(data);
    }

    pub fn try_extract_frame(&mut self) -> Option<Vec<u8>> {
        let data = self.recv_buffer.data();

        if data.len() < 2 {
            return None; // Not enough data for header
        }

        let fin = (data[0] & 0x80) != 0;
        let opcode = data[0] & 0x0F;
        let masked = (data[1] & 0x80) != 0;
        let mut payload_len = (data[1] & 0x7F) as u64;

        let mut header_size = 2;
        let mut offset = 2;

        // Extended payload length
        if payload_len == 126 {
            if data.len() < 4 {
                return None;
            }
            payload_len = u16::from_be_bytes([data[2], data[3]]) as u64;
            header_size = 4;
            offset = 4;
        } else if payload_len == 127 {
            if data.len() < 10 {
                return None;
            }
            payload_len = u64::from_be_bytes([
                data[2], data[3], data[4], data[5],
                data[6], data[7], data[8], data[9],
            ]);
            header_size = 10;
            offset = 10;
        }

        // Masking key
        let mask_key = if masked {
            if data.len() < offset + 4 {
                return None;
            }
            let key = [data[offset], data[offset + 1], data[offset + 2], data[offset + 3]];
            header_size += 4;
            offset += 4;
            Some(key)
        } else {
            None
        };

        // Check if we have complete frame
        let total_size = header_size + payload_len as usize;
        if data.len() < total_size {
            return None;
        }

        // Extract payload
        let mut payload = data[offset..offset + payload_len as usize].to_vec();

        // Unmask if needed
        if let Some(key) = mask_key {
            for (i, byte) in payload.iter_mut().enumerate() {
                *byte ^= key[i % 4];
            }
        }

        // Consume processed data
        self.recv_buffer.consume(total_size);

        Some(payload)
    }

    pub fn queue_send_data(&mut self, data: &[u8]) -> bool {
        self.send_buffer.write(data) == data.len()
    }

    pub fn get_send_data(&mut self, buffer: &mut [u8]) -> usize {
        self.send_buffer.read(buffer)
    }

    pub fn pending_send_size(&self) -> usize {
        self.send_buffer.size()
    }
}

impl Default for WebSocketFrameBuffer {
    fn default() -> Self {
        Self::new()
    }
}

// ============================================================================
// DEMONSTRATION
// ============================================================================

fn main() {
    println!("=== WebSocket Buffer Management (Rust) ===\n");

    // Test circular buffer
    println!("1. Circular Buffer Test:");
    let mut cb = CircularBuffer::new(64);
    
    let msg = b"Hello, WebSocket!";
    let written = cb.write(msg);
    println!("   Written: {} bytes", written);
    println!("   Buffer size: {}/{}", cb.size(), 64);
    
    let mut read_buf = [0u8; 10];
    let read = cb.read(&mut read_buf[..5]);
    println!("   Read: {} bytes", read);
    println!("   Remaining: {} bytes\n", cb.size());

    // Test dynamic buffer
    println!("2. Dynamic Buffer Test:");
    let mut db = DynamicBuffer::new(16);
    
    let msg2 = b"This message will grow the buffer!";
    db.append(msg2);
    println!("   Buffer size: {} bytes", db.size());
    
    db.consume(10);
    println!("   After consuming 10 bytes: {} bytes\n", db.size());

    // Test buffer pool
    println!("3. Buffer Pool Test:");
    let pool = BufferPool::new(1024, 4, 100);
    println!("   Initial pool size: {}", pool.pool_size());
    
    {
        let buf1 = pool.acquire();
        let buf2 = pool.acquire();
        println!("   After acquiring 2 buffers: {}", pool.pool_size());
        // Buffers are automatically returned when dropped
    }
    
    println!("   After releasing 2 buffers: {}\n", pool.pool_size());

    // Test WebSocket frame buffer
    println!("4. WebSocket Frame Buffer Test:");
    let mut ws_buffer = WebSocketFrameBuffer::new();
    
    // Simulate received data (simple unmasked text frame)
    let frame_data = vec![0x81, 0x05, b'H', b'e', b'l', b'l', b'o'];
    ws_buffer.add_received_data(&frame_data);
    
    if let Some(extracted_frame) = ws_buffer.try_extract_frame() {
        println!("   Extracted frame: {}", 
                 String::from_utf8_lossy(&extracted_frame));
    }

    println!("\n=== Demo Complete ===");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_circular_buffer_wrap_around() {
        let mut cb = CircularBuffer::new(8);
        
        cb.write(b"12345678");
        assert_eq!(cb.size(), 8);
        assert!(cb.is_full());
        
        let mut buf = [0u8; 4];
        cb.read(&mut buf);
        assert_eq!(&buf, b"1234");
        assert_eq!(cb.size(), 4);
        
        cb.write(b"ABCD");
        assert_eq!(cb.size(), 8);
        
        let mut result = [0u8; 8];
        cb.read(&mut result);
        assert_eq!(&result, b"5678ABCD");
    }

    #[test]
    fn test_dynamic_buffer_growth() {
        let mut db = DynamicBuffer::new(4);
        
        db.append(b"Hello, World!");
        assert!(db.size() >= 13);
        
        let consumed = db.consume(7);
        assert_eq!(consumed, 7);
        assert_eq!(db.data(), b"World!");
    }

    #[test]
    fn test_buffer_pool() {
        let pool = BufferPool::new(1024, 2, 10);
        assert_eq!(pool.pool_size(), 2);
        
        let _buf1 = pool.acquire();
        assert_eq!(pool.pool_size(), 1);
        
        drop(_buf1);
        assert_eq!(pool.pool_size(), 2);
    }
}