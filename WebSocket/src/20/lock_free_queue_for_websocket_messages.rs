use std::sync::atomic::{AtomicPtr, AtomicUsize, Ordering};
use std::ptr;
use std::sync::Arc;

// WebSocket message structure
#[derive(Clone, Debug)]
pub struct WebSocketMessage {
    pub data: Vec<u8>,
    pub connection_id: usize,
}

// Lock-free queue node
struct QueueNode {
    message: Option<WebSocketMessage>,
    next: AtomicPtr<QueueNode>,
}

impl QueueNode {
    fn new(message: WebSocketMessage) -> Self {
        QueueNode {
            message: Some(message),
            next: AtomicPtr::new(ptr::null_mut()),
        }
    }

    fn dummy() -> Self {
        QueueNode {
            message: None,
            next: AtomicPtr::new(ptr::null_mut()),
        }
    }
}

// Lock-free MPSC queue (Multiple Producer Single Consumer)
pub struct LockFreeQueue {
    head: AtomicPtr<QueueNode>,
    tail: AtomicPtr<QueueNode>,
    count: AtomicUsize,
}

impl LockFreeQueue {
    pub fn new() -> Arc<Self> {
        let dummy = Box::into_raw(Box::new(QueueNode::dummy()));
        
        Arc::new(LockFreeQueue {
            head: AtomicPtr::new(dummy),
            tail: AtomicPtr::new(dummy),
            count: AtomicUsize::new(0),
        })
    }

    /// Enqueue a message (safe for multiple producers)
    pub fn enqueue(&self, message: WebSocketMessage) {
        let node = Box::into_raw(Box::new(QueueNode::new(message)));

        loop {
            let tail = self.tail.load(Ordering::Acquire);
            let next = unsafe { (*tail).next.load(Ordering::Acquire) };

            // Check if tail is still the actual tail
            if tail == self.tail.load(Ordering::Acquire) {
                if next.is_null() {
                    // Try to link new node at the end
                    if unsafe {
                        (*tail).next.compare_exchange(
                            ptr::null_mut(),
                            node,
                            Ordering::Release,
                            Ordering::Acquire,
                        ).is_ok()
                    } {
                        // Successfully linked, try to swing tail to new node
                        let _ = self.tail.compare_exchange(
                            tail,
                            node,
                            Ordering::Release,
                            Ordering::Acquire,
                        );
                        
                        self.count.fetch_add(1, Ordering::Relaxed);
                        return;
                    }
                } else {
                    // Tail is lagging, help move it forward
                    let _ = self.tail.compare_exchange(
                        tail,
                        next,
                        Ordering::Release,
                        Ordering::Acquire,
                    );
                }
            }
        }
    }

    /// Dequeue a message (single consumer only)
    pub fn dequeue(&self) -> Option<WebSocketMessage> {
        loop {
            let head = self.head.load(Ordering::Acquire);
            let tail = self.tail.load(Ordering::Acquire);
            let next = unsafe { (*head).next.load(Ordering::Acquire) };

            if head == self.head.load(Ordering::Acquire) {
                if head == tail {
                    if next.is_null() {
                        return None; // Queue is empty
                    }
                    // Tail is lagging, help move it forward
                    let _ = self.tail.compare_exchange(
                        tail,
                        next,
                        Ordering::Release,
                        Ordering::Acquire,
                    );
                } else {
                    // Read value before CAS
                    let message = unsafe { (*next).message.clone() };
                    
                    // Try to swing head to next node
                    if self.head.compare_exchange(
                        head,
                        next,
                        Ordering::Release,
                        Ordering::Acquire,
                    ).is_ok() {
                        self.count.fetch_sub(1, Ordering::Relaxed);
                        
                        // Free old head
                        unsafe {
                            drop(Box::from_raw(head));
                        }
                        
                        return message;
                    }
                }
            }
        }
    }

    pub fn len(&self) -> usize {
        self.count.load(Ordering::Relaxed)
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

impl Drop for LockFreeQueue {
    fn drop(&mut self) {
        while self.dequeue().is_some() {}
        
        // Free dummy node
        let head = self.head.load(Ordering::Acquire);
        if !head.is_null() {
            unsafe {
                drop(Box::from_raw(head));
            }
        }
    }
}

unsafe impl Send for LockFreeQueue {}
unsafe impl Sync for LockFreeQueue {}

// ============ LOCK-FREE RING BUFFER (SPSC) ============

pub struct LockFreeRingBuffer {
    buffer: Vec<Option<WebSocketMessage>>,
    capacity: usize,
    write_pos: AtomicUsize,
    read_pos: AtomicUsize,
}

impl LockFreeRingBuffer {
    pub fn new(capacity: usize) -> Arc<Self> {
        let mut buffer = Vec::with_capacity(capacity);
        for _ in 0..capacity {
            buffer.push(None);
        }

        Arc::new(LockFreeRingBuffer {
            buffer,
            capacity,
            write_pos: AtomicUsize::new(0),
            read_pos: AtomicUsize::new(0),
        })
    }

    pub fn push(&self, message: WebSocketMessage) -> Result<(), WebSocketMessage> {
        let w = self.write_pos.load(Ordering::Relaxed);
        let r = self.read_pos.load(Ordering::Acquire);
        let next_w = (w + 1) % self.capacity;

        if next_w == r {
            return Err(message); // Buffer full
        }

        // Safety: We have exclusive access to this slot
        unsafe {
            let slot = &self.buffer[w] as *const Option<WebSocketMessage> 
                as *mut Option<WebSocketMessage>;
            *slot = Some(message);
        }

        self.write_pos.store(next_w, Ordering::Release);
        Ok(())
    }

    pub fn pop(&self) -> Option<WebSocketMessage> {
        let r = self.read_pos.load(Ordering::Relaxed);
        let w = self.write_pos.load(Ordering::Acquire);

        if r == w {
            return None; // Buffer empty
        }

        // Safety: We have exclusive access to this slot
        let message = unsafe {
            let slot = &self.buffer[r] as *const Option<WebSocketMessage> 
                as *mut Option<WebSocketMessage>;
            (*slot).take()
        };

        let next_r = (r + 1) % self.capacity;
        self.read_pos.store(next_r, Ordering::Release);

        message
    }

    pub fn len(&self) -> usize {
        let w = self.write_pos.load(Ordering::Relaxed);
        let r = self.read_pos.load(Ordering::Relaxed);
        
        if w >= r {
            w - r
        } else {
            self.capacity - r + w
        }
    }
}

unsafe impl Send for LockFreeRingBuffer {}
unsafe impl Sync for LockFreeRingBuffer {}

// ============ USAGE EXAMPLE ============

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    #[test]
    fn test_mpsc_queue() {
        let queue = LockFreeQueue::new();
        let mut handles = vec![];

        // Spawn multiple producer threads
        for thread_id in 0..4 {
            let q = Arc::clone(&queue);
            let handle = thread::spawn(move || {
                for i in 0..1000 {
                    let msg = WebSocketMessage {
                        data: format!("Message {} from thread {}", i, thread_id)
                            .into_bytes(),
                        connection_id: thread_id * 1000 + i,
                    };
                    q.enqueue(msg);
                }
            });
            handles.push(handle);
        }

        // Consumer thread
        let q = Arc::clone(&queue);
        let consumer = thread::spawn(move || {
            let mut processed = 0;
            let expected = 4000;

            while processed < expected {
                if let Some(msg) = q.dequeue() {
                    println!(
                        "Processed: conn={}, data={}",
                        msg.connection_id,
                        String::from_utf8_lossy(&msg.data)
                    );
                    processed += 1;
                } else {
                    thread::yield_now();
                }
            }
            processed
        });

        // Wait for producers
        for handle in handles {
            handle.join().unwrap();
        }

        let total = consumer.join().unwrap();
        println!("Total processed: {}", total);
        assert_eq!(total, 4000);
    }

    #[test]
    fn test_spsc_ring_buffer() {
        let buffer = LockFreeRingBuffer::new(1024);
        
        let producer = {
            let buf = Arc::clone(&buffer);
            thread::spawn(move || {
                for i in 0..10000 {
                    let msg = WebSocketMessage {
                        data: format!("Message {}", i).into_bytes(),
                        connection_id: i,
                    };
                    
                    while buf.push(msg.clone()).is_err() {
                        thread::yield_now();
                    }
                }
            })
        };

        let consumer = {
            let buf = Arc::clone(&buffer);
            thread::spawn(move || {
                let mut count = 0;
                while count < 10000 {
                    if let Some(_msg) = buf.pop() {
                        count += 1;
                    } else {
                        thread::yield_now();
                    }
                }
                count
            })
        };

        producer.join().unwrap();
        let total = consumer.join().unwrap();
        assert_eq!(total, 10000);
    }
}

// ============ REAL-WORLD WEBSOCKET EXAMPLE ============

use std::time::Duration;

pub struct WebSocketMessageBroker {
    incoming_queue: Arc<LockFreeQueue>,
    outgoing_buffers: Vec<Arc<LockFreeRingBuffer>>,
}

impl WebSocketMessageBroker {
    pub fn new(num_connections: usize, buffer_size: usize) -> Self {
        let mut outgoing_buffers = Vec::new();
        for _ in 0..num_connections {
            outgoing_buffers.push(LockFreeRingBuffer::new(buffer_size));
        }

        WebSocketMessageBroker {
            incoming_queue: LockFreeQueue::new(),
            outgoing_buffers,
        }
    }

    /// Called by connection threads to submit incoming messages
    pub fn submit_message(&self, message: WebSocketMessage) {
        self.incoming_queue.enqueue(message);
    }

    /// Processing thread that routes messages
    pub fn process_messages(&self) {
        loop {
            if let Some(msg) = self.incoming_queue.dequeue() {
                // Route message to appropriate connection
                let target_conn = msg.connection_id % self.outgoing_buffers.len();
                
                if let Some(buffer) = self.outgoing_buffers.get(target_conn) {
                    let _ = buffer.push(msg);
                }
            } else {
                thread::sleep(Duration::from_micros(100));
            }
        }
    }

    /// Connection thread retrieves messages for sending
    pub fn get_outgoing(&self, connection_id: usize) -> Option<WebSocketMessage> {
        self.outgoing_buffers
            .get(connection_id)
            .and_then(|buf| buf.pop())
    }
}