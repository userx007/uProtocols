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