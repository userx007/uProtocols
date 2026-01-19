# CPU Affinity and NUMA: Optimizing Thread Placement and Memory Locality

## Overview

CPU affinity and NUMA (Non-Uniform Memory Access) are critical concepts for optimizing performance in multi-core and multi-socket systems. While not exclusively WebSocket-related, these techniques are essential for building high-performance network servers that handle thousands of concurrent WebSocket connections efficiently.

## Understanding the Concepts

### CPU Affinity

CPU affinity allows you to bind threads or processes to specific CPU cores, preventing the operating system from migrating them across cores. This provides several benefits:

- **Cache locality**: Keeps thread data in the same CPU's cache hierarchy
- **Reduced context switching overhead**: Eliminates migration costs
- **Predictable performance**: Ensures consistent execution characteristics
- **Reduced cache thrashing**: Minimizes cache invalidation across cores

### NUMA Architecture

Modern multi-socket servers use NUMA architecture where:

- Each CPU socket has its own local memory
- Memory access is faster to local RAM than remote RAM (on another socket)
- The performance difference can be 2-3x or more
- Network interfaces may be closer to specific NUMA nodes

For WebSocket servers, this means:
- Network interrupts should be handled on the same NUMA node as the NIC
- Worker threads should access memory local to their NUMA node
- Connection data structures should be allocated on the correct node

## C/C++ Implementation

```cpp
#include <pthread.h>
#include <sched.h>
#include <numa.h>
#include <numaif.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <vector>
#include <memory>

// ============================================================================
// CPU Affinity Functions
// ============================================================================

// Set affinity for current thread to a specific core
int set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    pthread_t current_thread = pthread_self();
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    
    if (result != 0) {
        fprintf(stderr, "Failed to set thread affinity to core %d: %s\n", 
                core_id, strerror(result));
        return -1;
    }
    
    printf("Thread bound to CPU core %d\n", core_id);
    return 0;
}

// Get current thread's affinity
void print_thread_affinity() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    pthread_t current_thread = pthread_self();
    if (pthread_getaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) == 0) {
        printf("Thread can run on cores: ");
        for (int i = 0; i < CPU_SETSIZE; i++) {
            if (CPU_ISSET(i, &cpuset)) {
                printf("%d ", i);
            }
        }
        printf("\n");
    }
}

// Set affinity to multiple cores (useful for thread pools)
int set_thread_affinity_mask(const std::vector<int>& cores) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int core : cores) {
        CPU_SET(core, &cpuset);
    }
    
    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

// ============================================================================
// NUMA Functions
// ============================================================================

// Initialize NUMA library
void init_numa() {
    if (numa_available() < 0) {
        fprintf(stderr, "NUMA not available on this system\n");
        exit(1);
    }
    printf("NUMA nodes available: %d\n", numa_max_node() + 1);
}

// Print NUMA topology information
void print_numa_info() {
    int num_nodes = numa_max_node() + 1;
    int num_cpus = numa_num_configured_cpus();
    
    printf("\n=== NUMA Topology ===\n");
    printf("Number of NUMA nodes: %d\n", num_nodes);
    printf("Number of CPUs: %d\n", num_cpus);
    
    for (int node = 0; node < num_nodes; node++) {
        struct bitmask *cpus = numa_allocate_cpumask();
        numa_node_to_cpus(node, cpus);
        
        printf("\nNode %d CPUs: ", node);
        for (int cpu = 0; cpu < num_cpus; cpu++) {
            if (numa_bitmask_isbitset(cpus, cpu)) {
                printf("%d ", cpu);
            }
        }
        printf("\n");
        
        long long free_mem = numa_node_size64(node, NULL);
        printf("Node %d memory: %lld MB\n", node, free_mem / (1024 * 1024));
        
        numa_free_cpumask(cpus);
    }
}

// Allocate memory on specific NUMA node
void* allocate_on_node(size_t size, int node) {
    void *ptr = numa_alloc_onnode(size, node);
    if (ptr == NULL) {
        fprintf(stderr, "Failed to allocate %zu bytes on node %d\n", size, node);
        return NULL;
    }
    
    // Verify allocation is on correct node
    int actual_node = -1;
    get_mempolicy(&actual_node, NULL, 0, ptr, MPOL_F_NODE | MPOL_F_ADDR);
    printf("Allocated %zu bytes on node %d (actual: %d)\n", size, node, actual_node);
    
    return ptr;
}

// Free NUMA-allocated memory
void free_numa_memory(void *ptr, size_t size) {
    numa_free(ptr, size);
}

// Get NUMA node for a given CPU
int get_numa_node_for_cpu(int cpu) {
    return numa_node_of_cpu(cpu);
}

// ============================================================================
// WebSocket Worker Thread Example
// ============================================================================

struct WorkerConfig {
    int worker_id;
    int cpu_core;
    int numa_node;
    size_t buffer_size;
};

struct WorkerContext {
    int worker_id;
    void *local_buffer;  // NUMA-local buffer
    size_t buffer_size;
    // WebSocket connection pool would go here
};

void* websocket_worker_thread(void *arg) {
    WorkerConfig *config = (WorkerConfig*)arg;
    
    printf("\n=== Worker %d starting ===\n", config->worker_id);
    
    // Set CPU affinity
    if (set_thread_affinity(config->cpu_core) != 0) {
        return NULL;
    }
    
    // Allocate worker context on the local NUMA node
    WorkerContext *ctx = (WorkerContext*)allocate_on_node(
        sizeof(WorkerContext), config->numa_node);
    
    if (ctx == NULL) {
        return NULL;
    }
    
    ctx->worker_id = config->worker_id;
    ctx->buffer_size = config->buffer_size;
    
    // Allocate I/O buffer on local NUMA node for best performance
    ctx->local_buffer = allocate_on_node(config->buffer_size, config->numa_node);
    
    if (ctx->local_buffer == NULL) {
        free_numa_memory(ctx, sizeof(WorkerContext));
        return NULL;
    }
    
    printf("Worker %d: Running on core %d, NUMA node %d\n", 
           config->worker_id, config->cpu_core, config->numa_node);
    print_thread_affinity();
    
    // Simulate WebSocket processing
    // In real implementation, this would be the event loop
    sleep(2);
    
    // Cleanup
    free_numa_memory(ctx->local_buffer, config->buffer_size);
    free_numa_memory(ctx, sizeof(WorkerContext));
    
    return NULL;
}

// ============================================================================
// Main Example
// ============================================================================

int main() {
    // Initialize NUMA
    init_numa();
    print_numa_info();
    
    // Create worker threads, one per NUMA node
    int num_nodes = numa_max_node() + 1;
    std::vector<pthread_t> threads(num_nodes);
    std::vector<WorkerConfig> configs(num_nodes);
    
    printf("\n=== Creating %d worker threads ===\n", num_nodes);
    
    for (int i = 0; i < num_nodes; i++) {
        // Get first CPU of this NUMA node
        struct bitmask *cpus = numa_allocate_cpumask();
        numa_node_to_cpus(i, cpus);
        
        int cpu = -1;
        for (int c = 0; c < numa_num_configured_cpus(); c++) {
            if (numa_bitmask_isbitset(cpus, c)) {
                cpu = c;
                break;
            }
        }
        numa_free_cpumask(cpus);
        
        configs[i].worker_id = i;
        configs[i].cpu_core = cpu;
        configs[i].numa_node = i;
        configs[i].buffer_size = 64 * 1024; // 64KB buffer
        
        pthread_create(&threads[i], NULL, websocket_worker_thread, &configs[i]);
    }
    
    // Wait for all workers
    for (int i = 0; i < num_nodes; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\nAll workers completed\n");
    return 0;
}

// Compile with: g++ -o numa_example numa_example.cpp -lpthread -lnuma -std=c++11
// Run with: numactl --show; ./numa_example
```

## Rust Implementation

```rust
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
```

## Summary

### Key Concepts

**CPU Affinity** allows binding threads to specific CPU cores, which:
- Improves cache locality by keeping thread data in the same CPU's cache hierarchy
- Reduces context switching and migration overhead
- Provides more predictable performance characteristics
- Is essential for low-latency WebSocket servers handling many connections

**NUMA Architecture** in multi-socket systems means:
- Each CPU socket has local memory with faster access
- Remote memory access can be 2-3x slower than local access
- Network interfaces are typically closer to specific NUMA nodes
- Memory allocation should be NUMA-aware for optimal performance

### Implementation Approaches

**C/C++ Implementation:**
- Uses `pthread_setaffinity_np()` for CPU binding
- Uses `libnuma` for NUMA-aware memory allocation
- Provides fine-grained control over memory placement
- Requires manual memory management
- Best for maximum performance in production systems

**Rust Implementation:**
- Uses `core_affinity` crate for cross-platform CPU pinning
- Can use FFI to `libnuma` for Linux NUMA support
- Provides memory safety guarantees
- More portable across operating systems
- Excellent for building safe, high-performance network servers

### WebSocket Server Applications

For high-performance WebSocket servers:
1. **Pin worker threads** to specific CPU cores to maintain cache locality
2. **Allocate connection buffers** on the NUMA node local to each worker
3. **Distribute NIC interrupts** across NUMA nodes to balance load
4. **Use one worker per NUMA node** (or per core) to maximize locality
5. **Avoid cross-NUMA communication** when possible to prevent performance degradation

### Performance Impact

Proper CPU affinity and NUMA awareness can provide:
- **2-3x reduction** in memory access latency
- **20-40% improvement** in throughput for network-intensive workloads
- **More consistent latency** with reduced tail latencies
- **Better scalability** as system size increases

These optimizations are particularly important for WebSocket servers handling tens of thousands of concurrent connections, where every microsecond of latency and every bit of memory bandwidth matters.