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