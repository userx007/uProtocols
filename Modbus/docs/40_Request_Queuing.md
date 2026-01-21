# Request Queuing in Modbus

## Detailed Description

Request queuing is a critical architectural pattern in Modbus implementations that manages multiple communication requests by organizing them into a sequential processing queue. This approach ensures orderly, collision-free transmission over the shared communication medium while maintaining data integrity and preventing race conditions.

### Core Concepts

**Sequential Processing**: Modbus operates on a master-slave (client-server) paradigm where only one transaction can occur at a time on the bus. Request queuing serializes multiple concurrent requests from different parts of an application, ensuring they're processed one after another rather than simultaneously.

**Queue Management**: The system maintains a FIFO (First-In-First-Out) buffer that stores pending Modbus requests. Each request contains the target slave address, function code, register addresses, data values, and callback information for handling responses.

**Asynchronous Operation**: Request queuing enables non-blocking operation where application threads can submit requests without waiting for completion. The queue processor handles transmission, response reception, timeout management, and error handling independently.

**Priority and Fairness**: Advanced implementations may support priority levels, allowing critical requests (like emergency stops or alarms) to bypass normal queue order while maintaining fairness for standard operations.

### Benefits

- **Thread Safety**: Multiple application threads can safely submit requests without explicit synchronization
- **Resource Efficiency**: Prevents bus contention and transmission collisions
- **Error Recovery**: Centralized retry logic and timeout management
- **Performance**: Optimizes bus utilization through request batching and scheduling
- **Scalability**: Handles varying load conditions gracefully

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_QUEUE_SIZE 100
#define MAX_DATA_LENGTH 250
#define REQUEST_TIMEOUT_MS 1000

// Modbus function codes
typedef enum {
    FC_READ_COILS = 0x01,
    FC_READ_DISCRETE_INPUTS = 0x02,
    FC_READ_HOLDING_REGISTERS = 0x03,
    FC_READ_INPUT_REGISTERS = 0x04,
    FC_WRITE_SINGLE_COIL = 0x05,
    FC_WRITE_SINGLE_REGISTER = 0x06,
    FC_WRITE_MULTIPLE_REGISTERS = 0x10
} modbus_function_code_t;

// Request status
typedef enum {
    REQ_PENDING,
    REQ_PROCESSING,
    REQ_COMPLETED,
    REQ_TIMEOUT,
    REQ_ERROR
} request_status_t;

// Callback function type
typedef void (*response_callback_t)(void* user_data, request_status_t status, 
                                    uint8_t* response_data, size_t length);

// Modbus request structure
typedef struct {
    uint8_t slave_id;
    modbus_function_code_t function_code;
    uint16_t start_address;
    uint16_t quantity;
    uint8_t data[MAX_DATA_LENGTH];
    size_t data_length;
    
    // Response handling
    response_callback_t callback;
    void* user_data;
    
    // Status tracking
    request_status_t status;
    struct timespec timestamp;
    int retry_count;
    uint32_t transaction_id;
} modbus_request_t;

// Request queue structure
typedef struct {
    modbus_request_t* requests[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
    bool running;
    uint32_t next_transaction_id;
} modbus_queue_t;

// Initialize the queue
modbus_queue_t* modbus_queue_init(void) {
    modbus_queue_t* queue = (modbus_queue_t*)malloc(sizeof(modbus_queue_t));
    if (!queue) return NULL;
    
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->running = true;
    queue->next_transaction_id = 1;
    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond_not_empty, NULL);
    pthread_cond_init(&queue->cond_not_full, NULL);
    
    return queue;
}

// Enqueue a request
int modbus_queue_enqueue(modbus_queue_t* queue, modbus_request_t* request) {
    pthread_mutex_lock(&queue->mutex);
    
    // Wait if queue is full
    while (queue->count >= MAX_QUEUE_SIZE && queue->running) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }
    
    if (!queue->running) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // Assign transaction ID and timestamp
    request->transaction_id = queue->next_transaction_id++;
    request->status = REQ_PENDING;
    clock_gettime(CLOCK_MONOTONIC, &request->timestamp);
    
    // Add to queue
    queue->requests[queue->tail] = request;
    queue->tail = (queue->tail + 1) % MAX_QUEUE_SIZE;
    queue->count++;
    
    // Signal waiting consumers
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    printf("[Queue] Enqueued request #%u for slave %d, function 0x%02X\n",
           request->transaction_id, request->slave_id, request->function_code);
    
    return 0;
}

// Dequeue a request
modbus_request_t* modbus_queue_dequeue(modbus_queue_t* queue) {
    pthread_mutex_lock(&queue->mutex);
    
    // Wait if queue is empty
    while (queue->count == 0 && queue->running) {
        pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
    }
    
    if (!queue->running && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    
    // Remove from queue
    modbus_request_t* request = queue->requests[queue->head];
    queue->head = (queue->head + 1) % MAX_QUEUE_SIZE;
    queue->count--;
    
    // Signal waiting producers
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return request;
}

// Simulate Modbus transaction (replace with actual implementation)
int modbus_execute_request(modbus_request_t* request, uint8_t* response, size_t* resp_len) {
    // Simulate network delay
    struct timespec delay = {0, 50000000}; // 50ms
    nanosleep(&delay, NULL);
    
    // Simulate successful response
    response[0] = request->slave_id;
    response[1] = request->function_code;
    *resp_len = 2;
    
    // Simulate occasional errors
    if (rand() % 10 == 0) {
        return -1; // Error
    }
    
    return 0; // Success
}

// Request processor thread
void* request_processor_thread(void* arg) {
    modbus_queue_t* queue = (modbus_queue_t*)arg;
    uint8_t response[MAX_DATA_LENGTH];
    size_t response_length;
    
    printf("[Processor] Thread started\n");
    
    while (queue->running) {
        modbus_request_t* request = modbus_queue_dequeue(queue);
        if (!request) break;
        
        request->status = REQ_PROCESSING;
        printf("[Processor] Processing request #%u\n", request->transaction_id);
        
        // Execute the Modbus request
        int result = modbus_execute_request(request, response, &response_length);
        
        if (result == 0) {
            request->status = REQ_COMPLETED;
            if (request->callback) {
                request->callback(request->user_data, REQ_COMPLETED, 
                                response, response_length);
            }
            printf("[Processor] Request #%u completed successfully\n", 
                   request->transaction_id);
        } else {
            request->status = REQ_ERROR;
            if (request->callback) {
                request->callback(request->user_data, REQ_ERROR, NULL, 0);
            }
            printf("[Processor] Request #%u failed\n", request->transaction_id);
        }
        
        free(request);
    }
    
    printf("[Processor] Thread terminated\n");
    return NULL;
}

// Example callback function
void my_response_callback(void* user_data, request_status_t status,
                         uint8_t* response_data, size_t length) {
    int* request_id = (int*)user_data;
    printf("[Callback] Request %d: status=%d, response_length=%zu\n",
           *request_id, status, length);
}

// Cleanup queue
void modbus_queue_destroy(modbus_queue_t* queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->running = false;
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    
    free(queue);
}

// Demonstration
int main(void) {
    modbus_queue_t* queue = modbus_queue_init();
    pthread_t processor;
    
    // Start processor thread
    pthread_create(&processor, NULL, request_processor_thread, queue);
    
    // Enqueue several requests
    for (int i = 0; i < 5; i++) {
        modbus_request_t* req = (modbus_request_t*)malloc(sizeof(modbus_request_t));
        req->slave_id = 1;
        req->function_code = FC_READ_HOLDING_REGISTERS;
        req->start_address = 100 + i * 10;
        req->quantity = 5;
        req->callback = my_response_callback;
        req->user_data = malloc(sizeof(int));
        *(int*)req->user_data = i;
        req->retry_count = 0;
        
        modbus_queue_enqueue(queue, req);
    }
    
    // Wait for processing
    sleep(2);
    
    // Shutdown
    modbus_queue_destroy(queue);
    pthread_join(processor, NULL);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::VecDeque;
use std::sync::{Arc, Mutex, Condvar};
use std::thread;
use std::time::{Duration, Instant};

// Modbus function codes
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum ModbusFunctionCode {
    ReadCoils = 0x01,
    ReadDiscreteInputs = 0x02,
    ReadHoldingRegisters = 0x03,
    ReadInputRegisters = 0x04,
    WriteSingleCoil = 0x05,
    WriteSingleRegister = 0x06,
    WriteMultipleRegisters = 0x10,
}

// Request status
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RequestStatus {
    Pending,
    Processing,
    Completed,
    Timeout,
    Error,
}

// Response callback type
pub type ResponseCallback = Box<dyn FnOnce(RequestStatus, Option<Vec<u8>>) + Send>;

// Modbus request structure
pub struct ModbusRequest {
    pub slave_id: u8,
    pub function_code: ModbusFunctionCode,
    pub start_address: u16,
    pub quantity: u16,
    pub data: Vec<u8>,
    pub callback: Option<ResponseCallback>,
    pub status: RequestStatus,
    pub timestamp: Instant,
    pub retry_count: u32,
    pub transaction_id: u32,
}

impl ModbusRequest {
    pub fn new(
        slave_id: u8,
        function_code: ModbusFunctionCode,
        start_address: u16,
        quantity: u16,
    ) -> Self {
        Self {
            slave_id,
            function_code,
            start_address,
            quantity,
            data: Vec::new(),
            callback: None,
            status: RequestStatus::Pending,
            timestamp: Instant::now(),
            retry_count: 0,
            transaction_id: 0,
        }
    }

    pub fn with_callback(mut self, callback: ResponseCallback) -> Self {
        self.callback = Some(callback);
        self
    }

    pub fn with_data(mut self, data: Vec<u8>) -> Self {
        self.data = data;
        self
    }
}

// Request queue with thread-safe operations
pub struct ModbusQueue {
    queue: Arc<(Mutex<VecDeque<ModbusRequest>>, Condvar)>,
    max_size: usize,
    running: Arc<Mutex<bool>>,
    next_transaction_id: Arc<Mutex<u32>>,
}

impl ModbusQueue {
    pub fn new(max_size: usize) -> Self {
        Self {
            queue: Arc::new((Mutex::new(VecDeque::new()), Condvar::new())),
            max_size,
            running: Arc::new(Mutex::new(true)),
            next_transaction_id: Arc::new(Mutex::new(1)),
        }
    }

    pub fn enqueue(&self, mut request: ModbusRequest) -> Result<(), &'static str> {
        let (lock, cvar) = &*self.queue;
        let mut queue = lock.lock().unwrap();

        // Wait if queue is full
        while queue.len() >= self.max_size {
            let running = self.running.lock().unwrap();
            if !*running {
                return Err("Queue is shutting down");
            }
            drop(running);
            queue = cvar.wait(queue).unwrap();
        }

        // Assign transaction ID
        let mut next_id = self.next_transaction_id.lock().unwrap();
        request.transaction_id = *next_id;
        *next_id += 1;
        drop(next_id);

        request.status = RequestStatus::Pending;
        request.timestamp = Instant::now();

        println!(
            "[Queue] Enqueued request #{} for slave {}, function 0x{:02X}",
            request.transaction_id, request.slave_id, request.function_code as u8
        );

        queue.push_back(request);
        cvar.notify_one();

        Ok(())
    }

    pub fn dequeue(&self) -> Option<ModbusRequest> {
        let (lock, cvar) = &*self.queue;
        let mut queue = lock.lock().unwrap();

        // Wait if queue is empty
        while queue.is_empty() {
            let running = self.running.lock().unwrap();
            if !*running {
                return None;
            }
            drop(running);
            queue = cvar.wait(queue).unwrap();
        }

        let request = queue.pop_front();
        cvar.notify_one(); // Notify producers
        request
    }

    pub fn shutdown(&self) {
        let mut running = self.running.lock().unwrap();
        *running = false;
        drop(running);

        let (_, cvar) = &*self.queue;
        cvar.notify_all();
    }

    pub fn len(&self) -> usize {
        let (lock, _) = &*self.queue;
        lock.lock().unwrap().len()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

// Request processor
pub struct RequestProcessor {
    queue: Arc<ModbusQueue>,
}

impl RequestProcessor {
    pub fn new(queue: Arc<ModbusQueue>) -> Self {
        Self { queue }
    }

    // Simulate Modbus transaction
    fn execute_request(&self, request: &ModbusRequest) -> Result<Vec<u8>, &'static str> {
        // Simulate network delay
        thread::sleep(Duration::from_millis(50));

        // Simulate response
        let mut response = Vec::new();
        response.push(request.slave_id);
        response.push(request.function_code as u8);

        // Simulate occasional errors (10% failure rate)
        if rand::random::<u8>() % 10 == 0 {
            return Err("Communication error");
        }

        Ok(response)
    }

    pub fn start(&self) {
        let queue = Arc::clone(&self.queue);

        thread::spawn(move || {
            println!("[Processor] Thread started");

            loop {
                let mut request = match queue.dequeue() {
                    Some(req) => req,
                    None => break,
                };

                request.status = RequestStatus::Processing;
                println!("[Processor] Processing request #{}", request.transaction_id);

                let result = Self::execute_request_static(&request);

                match result {
                    Ok(response) => {
                        request.status = RequestStatus::Completed;
                        println!(
                            "[Processor] Request #{} completed successfully",
                            request.transaction_id
                        );
                        if let Some(callback) = request.callback {
                            callback(RequestStatus::Completed, Some(response));
                        }
                    }
                    Err(e) => {
                        request.status = RequestStatus::Error;
                        println!(
                            "[Processor] Request #{} failed: {}",
                            request.transaction_id, e
                        );
                        if let Some(callback) = request.callback {
                            callback(RequestStatus::Error, None);
                        }
                    }
                }
            }

            println!("[Processor] Thread terminated");
        });
    }

    fn execute_request_static(request: &ModbusRequest) -> Result<Vec<u8>, &'static str> {
        thread::sleep(Duration::from_millis(50));
        let mut response = Vec::new();
        response.push(request.slave_id);
        response.push(request.function_code as u8);
        if rand::random::<u8>() % 10 == 0 {
            return Err("Communication error");
        }
        Ok(response)
    }
}

// Demonstration
fn main() {
    let queue = Arc::new(ModbusQueue::new(100));
    let processor = RequestProcessor::new(Arc::clone(&queue));

    // Start processor
    processor.start();

    // Enqueue requests
    for i in 0..5 {
        let request = ModbusRequest::new(
            1,
            ModbusFunctionCode::ReadHoldingRegisters,
            100 + i * 10,
            5,
        )
        .with_callback(Box::new(move |status, response| {
            println!(
                "[Callback] Request {}: status={:?}, response={:?}",
                i, status, response
            );
        }));

        queue.enqueue(request).unwrap();
    }

    // Wait for processing
    thread::sleep(Duration::from_secs(2));

    // Shutdown
    queue.shutdown();
    thread::sleep(Duration::from_millis(100));

    println!("All requests processed");
}

// Add this at the top for the random function
mod rand {
    use std::cell::Cell;
    thread_local! {
        static RNG: Cell<u32> = Cell::new(12345);
    }
    pub fn random<T: From<u32>>() -> T {
        RNG.with(|rng| {
            let mut x = rng.get();
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            rng.set(x);
            T::from(x)
        })
    }
}
```

## Summary

Request queuing is an essential architectural component for robust Modbus implementations, particularly in multi-threaded environments or systems handling multiple concurrent operations. By serializing requests through a managed queue, developers ensure orderly bus access, prevent communication collisions, and maintain data integrity.

The pattern provides thread-safe enqueuing from multiple producers, sequential processing by a dedicated consumer thread, asynchronous operation with callback-based response handling, and centralized error management and retry logic. Both implementations demonstrate FIFO queue management with condition variables for thread synchronization, transaction ID tracking for request correlation, callback mechanisms for asynchronous response delivery, and graceful shutdown procedures.

In production systems, this architecture can be extended with priority queues for critical requests, adaptive timeout management based on network conditions, request batching for improved throughput, and comprehensive logging and diagnostics. Request queuing transforms Modbus communication from a blocking, error-prone operation into a reliable, scalable service suitable for industrial automation and monitoring systems.