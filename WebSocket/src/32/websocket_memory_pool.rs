use std::cell::RefCell;
use std::rc::Rc;
use std::sync::{Arc, Mutex};
use std::collections::VecDeque;

// Object pool with type safety
pub struct ObjectPool<T> {
    objects: Vec<Box<T>>,
    available: VecDeque<usize>,
    in_use: Vec<bool>,
    max_size: usize,
    allocations: usize,
    deallocations: usize,
}

impl<T: Default> ObjectPool<T> {
    pub fn new(initial_size: usize, max_size: usize) -> Self {
        let mut objects = Vec::with_capacity(initial_size);
        let mut available = VecDeque::with_capacity(initial_size);
        let mut in_use = Vec::with_capacity(initial_size);
        
        // Pre-allocate objects
        for i in 0..initial_size {
            objects.push(Box::new(T::default()));
            available.push_back(i);
            in_use.push(false);
        }
        
        ObjectPool {
            objects,
            available,
            in_use,
            max_size,
            allocations: 0,
            deallocations: 0,
        }
    }
    
    pub fn acquire(&mut self) -> Option<PooledObject<T>> {
        // Try to get from available pool
        if let Some(idx) = self.available.pop_front() {
            self.in_use[idx] = true;
            self.allocations += 1;
            return Some(PooledObject::new(idx));
        }
        
        // Expand pool if possible
        if self.objects.len() < self.max_size {
            let idx = self.objects.len();
            self.objects.push(Box::new(T::default()));
            self.in_use.push(true);
            self.allocations += 1;
            return Some(PooledObject::new(idx));
        }
        
        // Pool exhausted
        None
    }
    
    pub fn release(&mut self, obj: PooledObject<T>) {
        let idx = obj.index();
        if idx < self.in_use.len() && self.in_use[idx] {
            self.in_use[idx] = false;
            self.available.push_back(idx);
            self.deallocations += 1;
        }
    }
    
    pub fn get_mut(&mut self, obj: &PooledObject<T>) -> Option<&mut T> {
        let idx = obj.index();
        if idx < self.objects.len() && self.in_use[idx] {
            Some(&mut self.objects[idx])
        } else {
            None
        }
    }
    
    pub fn stats(&self) -> PoolStats {
        PoolStats {
            total_objects: self.objects.len(),
            available: self.available.len(),
            in_use: self.in_use.iter().filter(|&&x| x).count(),
            allocations: self.allocations,
            deallocations: self.deallocations,
        }
    }
}

// Handle to pooled object
pub struct PooledObject<T> {
    index: usize,
    _phantom: std::marker::PhantomData<T>,
}

impl<T> PooledObject<T> {
    fn new(index: usize) -> Self {
        PooledObject {
            index,
            _phantom: std::marker::PhantomData,
        }
    }
    
    fn index(&self) -> usize {
        self.index
    }
}

#[derive(Debug)]
pub struct PoolStats {
    pub total_objects: usize,
    pub available: usize,
    pub in_use: usize,
    pub allocations: usize,
    pub deallocations: usize,
}

// Thread-safe object pool
pub struct ThreadSafePool<T> {
    inner: Arc<Mutex<ObjectPool<T>>>,
}

impl<T: Default> ThreadSafePool<T> {
    pub fn new(initial_size: usize, max_size: usize) -> Self {
        ThreadSafePool {
            inner: Arc::new(Mutex::new(ObjectPool::new(initial_size, max_size))),
        }
    }
    
    pub fn acquire(&self) -> Option<ThreadSafePooledObject<T>> {
        let mut pool = self.inner.lock().unwrap();
        pool.acquire().map(|obj| {
            ThreadSafePooledObject {
                inner: obj,
                pool: Arc::clone(&self.inner),
            }
        })
    }
    
    pub fn stats(&self) -> PoolStats {
        self.inner.lock().unwrap().stats()
    }
}

impl<T> Clone for ThreadSafePool<T> {
    fn clone(&self) -> Self {
        ThreadSafePool {
            inner: Arc::clone(&self.inner),
        }
    }
}

// RAII wrapper for thread-safe pooled objects
pub struct ThreadSafePooledObject<T> {
    inner: PooledObject<T>,
    pool: Arc<Mutex<ObjectPool<T>>>,
}

impl<T> Drop for ThreadSafePooledObject<T> {
    fn drop(&mut self) {
        // Release back to pool when dropped
        let mut pool = self.pool.lock().unwrap();
        let idx = self.inner.index();
        if idx < pool.in_use.len() && pool.in_use[idx] {
            pool.in_use[idx] = false;
            pool.available.push_back(idx);
            pool.deallocations += 1;
        }
    }
}

impl<T> ThreadSafePooledObject<T> {
    pub fn get_mut(&mut self) -> Option<&mut T> {
        let mut pool = self.pool.lock().unwrap();
        let idx = self.inner.index();
        if idx < pool.objects.len() && pool.in_use[idx] {
            // SAFETY: We know only one reference exists due to mutex
            unsafe {
                let ptr = pool.objects[idx].as_mut() as *mut T;
                Some(&mut *ptr)
            }
        } else {
            None
        }
    }
}

// WebSocket frame structure
#[derive(Debug, Clone)]
pub enum Opcode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
}

#[derive(Default)]
pub struct WSFrame {
    pub payload: Vec<u8>,
    pub opcode: u8,
    pub fin: bool,
}

impl WSFrame {
    pub fn new(opcode: Opcode, fin: bool) -> Self {
        WSFrame {
            payload: Vec::new(),
            opcode: opcode as u8,
            fin,
        }
    }
    
    pub fn set_payload(&mut self, data: &[u8]) {
        self.payload.clear();
        self.payload.extend_from_slice(data);
    }
    
    pub fn clear(&mut self) {
        self.payload.clear();
        self.opcode = Opcode::Text as u8;
        self.fin = true;
    }
}

// Memory arena for temporary allocations
pub struct Arena {
    blocks: Vec<Vec<u8>>,
    current_block: usize,
    current_offset: usize,
    block_size: usize,
}

impl Arena {
    pub fn new(block_size: usize) -> Self {
        let mut blocks = Vec::new();
        blocks.push(vec![0u8; block_size]);
        
        Arena {
            blocks,
            current_block: 0,
            current_offset: 0,
            block_size,
        }
    }
    
    pub fn allocate(&mut self, size: usize) -> &mut [u8] {
        if self.current_offset + size > self.block_size {
            // Need new block
            self.blocks.push(vec![0u8; self.block_size]);
            self.current_block += 1;
            self.current_offset = 0;
        }
        
        let start = self.current_offset;
        self.current_offset += size;
        &mut self.blocks[self.current_block][start..start + size]
    }
    
    pub fn reset(&mut self) {
        self.current_block = 0;
        self.current_offset = 0;
    }
    
    pub fn total_allocated(&self) -> usize {
        self.blocks.len() * self.block_size
    }
}

// WebSocket connection with pooled resources
pub struct WSConnection {
    id: u64,
    arena: RefCell<Arena>,
}

impl WSConnection {
    pub fn new(id: u64) -> Self {
        WSConnection {
            id,
            arena: RefCell::new(Arena::new(64 * 1024)),
        }
    }
    
    pub fn allocate_temp(&self, size: usize) -> &mut [u8] {
        self.arena.borrow_mut().allocate(size)
    }
    
    pub fn reset_temp(&self) {
        self.arena.borrow_mut().reset();
    }
    
    pub fn id(&self) -> u64 {
        self.id
    }
}

fn main() {
    println!("WebSocket Memory Pool Demo");
    println!("===========================\n");
    
    // Create thread-safe frame pool
    let frame_pool = ThreadSafePool::<WSFrame>::new(50, 500);
    
    println!("Initial pool stats: {:?}\n", frame_pool.stats());
    
    // Create connections
    let mut connections: Vec<WSConnection> = (0..10)
        .map(|i| WSConnection::new(i))
        .collect();
    
    // Simulate message processing
    let mut active_frames: Vec<ThreadSafePooledObject<WSFrame>> = Vec::new();
    
    for round in 0..3 {
        println!("--- Round {} ---", round + 1);
        
        // Each connection sends messages
        for conn in &connections {
            if let Some(mut frame) = frame_pool.acquire() {
                if let Some(f) = frame.get_mut() {
                    f.opcode = Opcode::Text as u8;
                    let msg = format!("Message from connection {}", conn.id());
                    f.set_payload(msg.as_bytes());
                }
                active_frames.push(frame);
            }
        }
        
        println!("Active frames: {}", active_frames.len());
        println!("Pool stats: {:?}", frame_pool.stats());
        
        // Clear half the frames (simulating processing)
        if round > 0 {
            let mid = active_frames.len() / 2;
            active_frames.drain(0..mid);
        }
        
        println!();
    }
    
    // Cleanup
    active_frames.clear();
    println!("After cleanup:");
    println!("Pool stats: {:?}", frame_pool.stats());
    
    // Demonstrate arena allocation
    println!("\n--- Arena Allocation Demo ---");
    let conn = WSConnection::new(100);
    
    for i in 0..5 {
        let buffer = conn.allocate_temp(1024);
        let msg = format!("Temporary buffer {}", i);
        buffer[0..msg.len()].copy_from_slice(msg.as_bytes());
        println!("Allocated temp buffer {}", i);
    }
    
    conn.reset_temp();
    println!("Arena reset - memory reused");
}