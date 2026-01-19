use core_affinity::{CoreId, get_core_ids, set_for_current};
use std::thread;
use std::sync::Arc;
use std::time::Duration;

// For NUMA support on Linux, we'd typically use FFI to libnuma
// This example shows CPU affinity which works cross-platform
// and demonstrates the concepts applicable to NUMA-aware programming

#[cfg(target_os = "linux")]
mod numa_linux {
    use libc::{cpu_set_t, sched_setaffinity, CPU_SET, CPU_ZERO};
    use std::mem;
    
    pub fn set_cpu_affinity(cpu: usize) -> Result<(), String> {
        unsafe {
            let mut cpuset: cpu_set_t = mem::zeroed();
            CPU_ZERO(&mut cpuset);
            CPU_SET(cpu, &mut cpuset);
            
            let result = sched_setaffinity(
                0, // 0 means current thread
                mem::size_of::<cpu_set_t>(),
                &cpuset,
            );
            
            if result == 0 {
                Ok(())
            } else {
                Err(format!("Failed to set CPU affinity to core {}", cpu))
            }
        }
    }
}

// ============================================================================
// Thread Pool with CPU Affinity
// ============================================================================

struct AffinityWorker {
    id: usize,
    core_id: CoreId,
    handle: Option<thread::JoinHandle<()>>,
}

impl AffinityWorker {
    fn new(id: usize, core_id: CoreId) -> Self {
        AffinityWorker {
            id,
            core_id,
            handle: None,
        }
    }
    
    fn spawn<F>(&mut self, work: F) 
    where
        F: FnOnce(usize, CoreId) + Send + 'static,
    {
        let id = self.id;
        let core_id = self.core_id;
        
        self.handle = Some(thread::spawn(move || {
            // Pin this thread to the specific core
            if !set_for_current(core_id) {
                eprintln!("Warning: Failed to set affinity for worker {}", id);
            }
            
            println!("Worker {} pinned to core {:?}", id, core_id);
            
            // Execute the work
            work(id, core_id);
        }));
    }
    
    fn join(mut self) {
        if let Some(handle) = self.handle.take() {
            handle.join().unwrap();
        }
    }
}

// ============================================================================
// NUMA-Aware Data Structure Example
// ============================================================================

// In a real NUMA system, each worker would have its own local memory
// This simulates that concept with separate buffers per worker
struct NumaAwareBufferPool {
    buffers: Vec<Vec<u8>>,
    buffer_size: usize,
}

impl NumaAwareBufferPool {
    fn new(num_workers: usize, buffer_size: usize) -> Self {
        println!("\nAllocating {} buffers of {} bytes each", 
                 num_workers, buffer_size);
        
        let mut buffers = Vec::with_capacity(num_workers);
        for i in 0..num_workers {
            // In real NUMA code, we'd use numa_alloc_onnode here
            // Each buffer would be allocated on the NUMA node
            // corresponding to the worker's CPU
            let buffer = vec![0u8; buffer_size];
            buffers.push(buffer);
            println!("Buffer {} allocated", i);
        }
        
        NumaAwareBufferPool {
            buffers,
            buffer_size,
        }
    }
    
    fn get_buffer(&mut self, worker_id: usize) -> Option<&mut Vec<u8>> {
        self.buffers.get_mut(worker_id)
    }
}

// ============================================================================
// WebSocket Worker Simulation
// ============================================================================

struct WebSocketWorkerConfig {
    worker_id: usize,
    core_id: CoreId,
    connections_per_worker: usize,
}

fn websocket_worker(config: WebSocketWorkerConfig, buffer: Arc<Vec<u8>>) {
    println!("\n=== Worker {} Configuration ===", config.worker_id);
    println!("Core: {:?}", config.core_id);
    println!("Max connections: {}", config.connections_per_worker);
    println!("Buffer size: {} KB", buffer.len() / 1024);
    
    // Simulate WebSocket event loop
    // In real implementation, this would:
    // 1. Accept connections
    // 2. Process WebSocket frames
    // 3. Manage send/receive buffers (using local NUMA memory)
    
    for i in 0..5 {
        // Simulate processing WebSocket messages
        thread::sleep(Duration::from_millis(100));
        
        // Access local buffer (NUMA-local in real implementation)
        let _data = &buffer[0..1024];
        
        if i % 2 == 0 {
            println!("Worker {}: Processing messages (iteration {})", 
                     config.worker_id, i);
        }
    }
    
    println!("Worker {} completed", config.worker_id);
}

// ============================================================================
// CPU Topology Discovery
// ============================================================================

fn discover_cpu_topology() {
    println!("=== CPU Topology Discovery ===");
    
    match get_core_ids() {
        Some(core_ids) => {
            println!("Available CPU cores: {}", core_ids.len());
            for (idx, core_id) in core_ids.iter().enumerate() {
                println!("  Core {}: {:?}", idx, core_id);
            }
        }
        None => {
            println!("Failed to get core IDs");
        }
    }
}

// ============================================================================
// Main Example
// ============================================================================

fn main() {
    println!("CPU Affinity and NUMA Demonstration\n");
    
    discover_cpu_topology();
    
    // Get available cores
    let core_ids = get_core_ids().expect("Failed to get core IDs");
    let num_workers = core_ids.len().min(4); // Use up to 4 workers
    
    println!("\n=== Creating {} workers ===", num_workers);
    
    // Create NUMA-aware buffer pool
    // In real NUMA code, each buffer would be allocated on the 
    // NUMA node corresponding to its worker's CPU
    let buffer_size = 64 * 1024; // 64 KB per worker
    let mut buffer_pool = NumaAwareBufferPool::new(num_workers, buffer_size);
    
    // Create workers with CPU affinity
    let mut workers = Vec::new();
    
    for i in 0..num_workers {
        let core_id = core_ids[i];
        let mut worker = AffinityWorker::new(i, core_id);
        
        // Get reference to this worker's local buffer
        let buffer = buffer_pool.get_buffer(i)
            .expect("Failed to get buffer")
            .clone();
        let buffer_arc = Arc::new(buffer);
        
        let config = WebSocketWorkerConfig {
            worker_id: i,
            core_id,
            connections_per_worker: 1000,
        };
        
        worker.spawn(move |_id, _core| {
            websocket_worker(config, buffer_arc);
        });
        
        workers.push(worker);
    }
    
    // Wait for all workers to complete
    println!("\nWaiting for workers to complete...");
    for worker in workers {
        worker.join();
    }
    
    println!("\n=== All workers completed ===");
}

// ============================================================================
// Advanced NUMA Example (Linux-specific with FFI)
// ============================================================================

#[cfg(target_os = "linux")]
mod advanced_numa {
    use std::alloc::{alloc, dealloc, Layout};
    
    // This would require linking with libnuma
    // extern "C" {
    //     fn numa_available() -> i32;
    //     fn numa_max_node() -> i32;
    //     fn numa_node_of_cpu(cpu: i32) -> i32;
    //     fn numa_alloc_onnode(size: usize, node: i32) -> *mut u8;
    //     fn numa_free(mem: *mut u8, size: usize);
    // }
    
    pub struct NumaBuffer {
        ptr: *mut u8,
        size: usize,
        node: i32,
    }
    
    impl NumaBuffer {
        pub fn new(size: usize, node: i32) -> Option<Self> {
            // In real code, this would call numa_alloc_onnode
            // For demonstration, we use standard allocation
            unsafe {
                let layout = Layout::from_size_align(size, 64)
                    .ok()?;
                let ptr = alloc(layout);
                
                if ptr.is_null() {
                    None
                } else {
                    Some(NumaBuffer { ptr, size, node })
                }
            }
        }
        
        pub fn as_slice_mut(&mut self) -> &mut [u8] {
            unsafe {
                std::slice::from_raw_parts_mut(self.ptr, self.size)
            }
        }
    }
    
    impl Drop for NumaBuffer {
        fn drop(&mut self) {
            unsafe {
                let layout = Layout::from_size_align(self.size, 64)
                    .unwrap();
                dealloc(self.ptr, layout);
            }
        }
    }
}

/*
Dependencies for Cargo.toml:

[dependencies]
core_affinity = "0.8"
libc = "0.2"

# Optional for more advanced NUMA features:
# numa = "0.1"

For full NUMA support on Linux, you would also link with libnuma:
[build-dependencies]
pkg-config = "0.3"

And in build.rs:
pkg_config::probe_library("numa").unwrap();
*/