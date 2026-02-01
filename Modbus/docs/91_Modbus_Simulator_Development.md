# Modbus Simulator Development: Creating Virtual Slaves for Testing

## Overview

Modbus simulator development involves creating software-based virtual Modbus devices (slaves/servers) that emulate the behavior of real hardware without requiring physical equipment. This is essential for developing, testing, and validating Modbus master/client applications in a controlled environment before deployment to production systems.

## Key Concepts

### Why Modbus Simulators?

1. **Cost Reduction**: Eliminates need for physical hardware during development
2. **Rapid Prototyping**: Quickly test different device configurations
3. **Edge Case Testing**: Simulate error conditions and unusual scenarios
4. **Parallel Development**: Multiple developers can work simultaneously
5. **Automated Testing**: Enable CI/CD pipelines for Modbus applications
6. **Training**: Safe environment for learning Modbus protocols

### Simulator Components

A typical Modbus simulator includes:
- **Register Banks**: Coils, discrete inputs, holding registers, input registers
- **Protocol Handler**: Processes Modbus requests and generates responses
- **Configuration Interface**: Define register maps and behaviors
- **State Management**: Maintain device state between requests
- **Error Injection**: Simulate communication failures and exceptions

## C/C++ Implementation

### Basic Modbus TCP Simulator using libmodbus

```c
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <modbus.h>
#include <string.h>
#include <unistd.h>

#define SERVER_PORT 502
#define MAX_COILS 100
#define MAX_DISCRETE_INPUTS 100
#define MAX_HOLDING_REGISTERS 100
#define MAX_INPUT_REGISTERS 100

typedef struct {
    modbus_t *ctx;
    modbus_mapping_t *mapping;
    int server_socket;
    int is_running;
} modbus_simulator_t;

// Initialize the simulator
modbus_simulator_t* simulator_init(int port) {
    modbus_simulator_t *sim = malloc(sizeof(modbus_simulator_t));
    if (!sim) return NULL;
    
    // Create Modbus TCP context
    sim->ctx = modbus_new_tcp("0.0.0.0", port);
    if (!sim->ctx) {
        fprintf(stderr, "Failed to create Modbus context\n");
        free(sim);
        return NULL;
    }
    
    // Allocate memory for registers
    sim->mapping = modbus_mapping_new(
        MAX_COILS,              // Coils
        MAX_DISCRETE_INPUTS,    // Discrete inputs
        MAX_HOLDING_REGISTERS,  // Holding registers
        MAX_INPUT_REGISTERS     // Input registers
    );
    
    if (!sim->mapping) {
        fprintf(stderr, "Failed to allocate mapping\n");
        modbus_free(sim->ctx);
        free(sim);
        return NULL;
    }
    
    sim->is_running = 0;
    return sim;
}

// Initialize register values with test data
void simulator_init_registers(modbus_simulator_t *sim) {
    // Initialize some holding registers with test values
    for (int i = 0; i < 10; i++) {
        sim->mapping->tab_registers[i] = i * 100;
    }
    
    // Initialize input registers (e.g., sensor values)
    sim->mapping->tab_input_registers[0] = 2500; // Temperature: 25.00°C
    sim->mapping->tab_input_registers[1] = 6540; // Humidity: 65.40%
    sim->mapping->tab_input_registers[2] = 1013; // Pressure: 1013 mbar
    
    // Set some coils
    modbus_set_bits_from_byte(sim->mapping->tab_bits, 0, 0b10101010);
    
    // Set discrete inputs
    modbus_set_bits_from_byte(sim->mapping->tab_input_bits, 0, 0b11001100);
}

// Main server loop
void simulator_run(modbus_simulator_t *sim) {
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
    int master_socket;
    fd_set refset;
    fd_set rdset;
    int fdmax;
    
    // Listen for connections
    sim->server_socket = modbus_tcp_listen(sim->ctx, 1);
    if (sim->server_socket == -1) {
        fprintf(stderr, "Failed to listen: %s\n", modbus_strerror(errno));
        return;
    }
    
    printf("Modbus TCP Simulator running on port %d\n", SERVER_PORT);
    
    FD_ZERO(&refset);
    FD_SET(sim->server_socket, &refset);
    fdmax = sim->server_socket;
    
    sim->is_running = 1;
    
    while (sim->is_running) {
        rdset = refset;
        
        if (select(fdmax + 1, &rdset, NULL, NULL, NULL) == -1) {
            perror("Server select() failure");
            break;
        }
        
        // Handle new connections
        if (FD_ISSET(sim->server_socket, &rdset)) {
            socklen_t addrlen;
            struct sockaddr_in clientaddr;
            int newfd;
            
            addrlen = sizeof(clientaddr);
            newfd = accept(sim->server_socket, 
                          (struct sockaddr *)&clientaddr, 
                          &addrlen);
            
            if (newfd == -1) {
                perror("Server accept() error");
            } else {
                FD_SET(newfd, &refset);
                if (newfd > fdmax) {
                    fdmax = newfd;
                }
                printf("New connection from %s\n", 
                       inet_ntoa(clientaddr.sin_addr));
            }
        }
        
        // Handle data from existing connections
        for (master_socket = sim->server_socket + 1; 
             master_socket <= fdmax; 
             master_socket++) {
            
            if (!FD_ISSET(master_socket, &rdset)) {
                continue;
            }
            
            modbus_set_socket(sim->ctx, master_socket);
            int rc = modbus_receive(sim->ctx, query);
            
            if (rc > 0) {
                // Process the request and send response
                modbus_reply(sim->ctx, query, rc, sim->mapping);
                
                // Log the request
                printf("Processed request, function code: 0x%02X\n", 
                       query[7]);
            } else if (rc == -1) {
                // Connection closed or error
                printf("Connection closed\n");
                close(master_socket);
                FD_CLR(master_socket, &refset);
                
                if (master_socket == fdmax) {
                    fdmax--;
                }
            }
        }
    }
}

// Cleanup
void simulator_cleanup(modbus_simulator_t *sim) {
    if (sim) {
        if (sim->server_socket != -1) {
            close(sim->server_socket);
        }
        if (sim->mapping) {
            modbus_mapping_free(sim->mapping);
        }
        if (sim->ctx) {
            modbus_close(sim->ctx);
            modbus_free(sim->ctx);
        }
        free(sim);
    }
}

int main(void) {
    modbus_simulator_t *simulator = simulator_init(SERVER_PORT);
    if (!simulator) {
        return 1;
    }
    
    simulator_init_registers(simulator);
    simulator_run(simulator);
    simulator_cleanup(simulator);
    
    return 0;
}
```

### Advanced Simulator with Dynamic Behavior

```cpp
#include <iostream>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <modbus.h>

class ModbusSimulator {
private:
    modbus_t* ctx;
    modbus_mapping_t* mapping;
    int server_socket;
    std::atomic<bool> running;
    std::vector<std::function<void()>> update_callbacks;
    
public:
    ModbusSimulator(int port = 502) : running(false) {
        ctx = modbus_new_tcp("0.0.0.0", port);
        mapping = modbus_mapping_new(100, 100, 100, 100);
    }
    
    ~ModbusSimulator() {
        stop();
        if (mapping) modbus_mapping_free(mapping);
        if (ctx) {
            modbus_close(ctx);
            modbus_free(ctx);
        }
    }
    
    // Add a callback for dynamic register updates
    void addUpdateCallback(std::function<void()> callback) {
        update_callbacks.push_back(callback);
    }
    
    // Simulate a temperature sensor with drift
    void simulateTemperatureSensor(int register_addr) {
        addUpdateCallback([this, register_addr]() {
            static float temp = 25.0f;
            // Add small random variation
            temp += ((rand() % 20) - 10) / 100.0f;
            
            // Keep within realistic bounds
            if (temp < 20.0f) temp = 20.0f;
            if (temp > 30.0f) temp = 30.0f;
            
            // Store as scaled integer (temp * 100)
            mapping->tab_input_registers[register_addr] = 
                static_cast<uint16_t>(temp * 100);
        });
    }
    
    // Simulate a counter that increments
    void simulateCounter(int register_addr, int increment = 1) {
        addUpdateCallback([this, register_addr, increment]() {
            mapping->tab_registers[register_addr] += increment;
        });
    }
    
    // Update dynamic values
    void updateDynamicValues() {
        for (auto& callback : update_callbacks) {
            callback();
        }
    }
    
    void start() {
        server_socket = modbus_tcp_listen(ctx, 5);
        if (server_socket == -1) {
            std::cerr << "Listen failed\n";
            return;
        }
        
        running = true;
        std::cout << "Modbus simulator started\n";
        
        // Start update thread for dynamic values
        std::thread update_thread([this]() {
            while (running) {
                updateDynamicValues();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
        
        // Main server loop
        while (running) {
            uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
            int client_socket = modbus_tcp_accept(ctx, &server_socket);
            
            if (client_socket == -1) {
                continue;
            }
            
            modbus_set_socket(ctx, client_socket);
            
            while (running) {
                int rc = modbus_receive(ctx, query);
                if (rc > 0) {
                    modbus_reply(ctx, query, rc, mapping);
                } else {
                    break;
                }
            }
            
            close(client_socket);
        }
        
        update_thread.join();
    }
    
    void stop() {
        running = false;
    }
    
    // Direct register access for testing
    void setHoldingRegister(int addr, uint16_t value) {
        if (addr < 100) {
            mapping->tab_registers[addr] = value;
        }
    }
    
    uint16_t getHoldingRegister(int addr) {
        return addr < 100 ? mapping->tab_registers[addr] : 0;
    }
};

int main() {
    ModbusSimulator simulator(502);
    
    // Configure simulated devices
    simulator.simulateTemperatureSensor(0);  // Temperature at register 30000
    simulator.simulateTemperatureSensor(1);  // Another sensor at 30001
    simulator.simulateCounter(0, 1);          // Counter at register 40000
    
    // Set initial values
    simulator.setHoldingRegister(10, 1234);
    
    // Run the simulator
    simulator.start();
    
    return 0;
}
```

## Rust Implementation

### Basic Modbus TCP Simulator using tokio-modbus

```rust
use tokio::net::TcpListener;
use tokio_modbus::prelude::*;
use tokio_modbus::server::tcp::Server;
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::net::SocketAddr;

// Simulated device state
#[derive(Debug, Clone)]
struct ModbusDeviceState {
    coils: Vec<bool>,
    discrete_inputs: Vec<bool>,
    holding_registers: Vec<u16>,
    input_registers: Vec<u16>,
}

impl ModbusDeviceState {
    fn new() -> Self {
        Self {
            coils: vec![false; 100],
            discrete_inputs: vec![false; 100],
            holding_registers: vec![0; 100],
            input_registers: vec![0; 100],
        }
    }
    
    fn init_test_data(&mut self) {
        // Initialize with test data
        for i in 0..10 {
            self.holding_registers[i] = (i as u16) * 100;
        }
        
        // Simulate sensor values
        self.input_registers[0] = 2500; // Temperature: 25.00°C
        self.input_registers[1] = 6540; // Humidity: 65.40%
        self.input_registers[2] = 1013; // Pressure: 1013 mbar
        
        // Set some coils
        self.coils[0] = true;
        self.coils[2] = true;
        self.coils[4] = true;
    }
}

// Custom service implementation
struct SimulatorService {
    state: Arc<Mutex<ModbusDeviceState>>,
}

impl SimulatorService {
    fn new(state: Arc<Mutex<ModbusDeviceState>>) -> Self {
        Self { state }
    }
}

impl tokio_modbus::server::Service for SimulatorService {
    type Request = Request;
    type Response = Response;
    type Error = std::io::Error;
    type Future = std::pin::Pin<Box<dyn std::future::Future<Output = Result<Self::Response, Self::Error>> + Send>>;

    fn call(&self, req: Self::Request) -> Self::Future {
        let state = self.state.clone();
        
        Box::pin(async move {
            let mut device_state = state.lock().unwrap();
            
            let response = match req {
                Request::ReadCoils(addr, cnt) => {
                    let addr = addr as usize;
                    let cnt = cnt as usize;
                    let coils = device_state.coils[addr..addr + cnt].to_vec();
                    Response::ReadCoils(coils)
                }
                
                Request::ReadDiscreteInputs(addr, cnt) => {
                    let addr = addr as usize;
                    let cnt = cnt as usize;
                    let inputs = device_state.discrete_inputs[addr..addr + cnt].to_vec();
                    Response::ReadDiscreteInputs(inputs)
                }
                
                Request::ReadHoldingRegisters(addr, cnt) => {
                    let addr = addr as usize;
                    let cnt = cnt as usize;
                    let regs = device_state.holding_registers[addr..addr + cnt].to_vec();
                    Response::ReadHoldingRegisters(regs)
                }
                
                Request::ReadInputRegisters(addr, cnt) => {
                    let addr = addr as usize;
                    let cnt = cnt as usize;
                    let regs = device_state.input_registers[addr..addr + cnt].to_vec();
                    Response::ReadInputRegisters(regs)
                }
                
                Request::WriteSingleCoil(addr, val) => {
                    device_state.coils[addr as usize] = val;
                    Response::WriteSingleCoil(addr, val)
                }
                
                Request::WriteSingleRegister(addr, val) => {
                    device_state.holding_registers[addr as usize] = val;
                    Response::WriteSingleRegister(addr, val)
                }
                
                Request::WriteMultipleCoils(addr, values) => {
                    let addr = addr as usize;
                    for (i, &val) in values.iter().enumerate() {
                        device_state.coils[addr + i] = val;
                    }
                    Response::WriteMultipleCoils(addr as u16, values.len() as u16)
                }
                
                Request::WriteMultipleRegisters(addr, values) => {
                    let addr = addr as usize;
                    for (i, &val) in values.iter().enumerate() {
                        device_state.holding_registers[addr + i] = val;
                    }
                    Response::WriteMultipleRegisters(addr as u16, values.len() as u16)
                }
                
                _ => {
                    return Err(std::io::Error::new(
                        std::io::ErrorKind::InvalidInput,
                        "Unsupported function code"
                    ));
                }
            };
            
            println!("Processed request: {:?}", req);
            Ok(response)
        })
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let socket_addr = "127.0.0.1:502".parse::<SocketAddr>()?;
    
    // Create device state
    let state = Arc::new(Mutex::new(ModbusDeviceState::new()));
    state.lock().unwrap().init_test_data();
    
    println!("Starting Modbus TCP simulator on {}", socket_addr);
    
    // Create TCP listener
    let listener = TcpListener::bind(socket_addr).await?;
    
    // Start accepting connections
    loop {
        let (stream, client_addr) = listener.accept().await?;
        println!("New connection from: {}", client_addr);
        
        let service = SimulatorService::new(state.clone());
        let server = Server::new(stream);
        
        tokio::spawn(async move {
            if let Err(e) = server.serve(service).await {
                eprintln!("Server error: {}", e);
            }
        });
    }
}
```

### Advanced Rust Simulator with Dynamic Updates

```rust
use tokio::time::{interval, Duration};
use rand::Rng;
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Debug, Clone)]
struct DynamicSimulator {
    state: Arc<RwLock<ModbusDeviceState>>,
}

impl DynamicSimulator {
    fn new() -> Self {
        let mut state = ModbusDeviceState::new();
        state.init_test_data();
        
        Self {
            state: Arc::new(RwLock::new(state)),
        }
    }
    
    // Simulate temperature sensor with realistic variation
    async fn simulate_temperature(&self, register_addr: usize) {
        let mut interval = interval(Duration::from_secs(1));
        let mut temp = 25.0f32;
        let mut rng = rand::thread_rng();
        
        loop {
            interval.tick().await;
            
            // Add random variation
            temp += rng.gen_range(-0.1..0.1);
            
            // Keep within bounds
            temp = temp.clamp(20.0, 30.0);
            
            // Update register (scaled by 100)
            let mut state = self.state.write().await;
            state.input_registers[register_addr] = (temp * 100.0) as u16;
        }
    }
    
    // Simulate a running counter
    async fn simulate_counter(&self, register_addr: usize, increment: u16) {
        let mut interval = interval(Duration::from_secs(1));
        
        loop {
            interval.tick().await;
            
            let mut state = self.state.write().await;
            state.holding_registers[register_addr] = 
                state.holding_registers[register_addr].wrapping_add(increment);
        }
    }
    
    // Simulate digital input changes (e.g., door sensor)
    async fn simulate_digital_input(&self, input_addr: usize) {
        let mut interval = interval(Duration::from_secs(5));
        let mut rng = rand::thread_rng();
        
        loop {
            interval.tick().await;
            
            let mut state = self.state.write().await;
            // Randomly toggle the input
            if rng.gen_bool(0.3) {
                state.discrete_inputs[input_addr] = 
                    !state.discrete_inputs[input_addr];
                println!("Digital input {} changed to {}", 
                        input_addr, 
                        state.discrete_inputs[input_addr]);
            }
        }
    }
    
    // Start all simulations
    async fn start_simulations(&self) {
        // Spawn temperature simulation tasks
        let sim1 = self.clone();
        tokio::spawn(async move {
            sim1.simulate_temperature(0).await;
        });
        
        let sim2 = self.clone();
        tokio::spawn(async move {
            sim2.simulate_temperature(1).await;
        });
        
        // Spawn counter simulation
        let sim3 = self.clone();
        tokio::spawn(async move {
            sim3.simulate_counter(0, 1).await;
        });
        
        // Spawn digital input simulation
        let sim4 = self.clone();
        tokio::spawn(async move {
            sim4.simulate_digital_input(0).await;
        });
    }
    
    fn get_state(&self) -> Arc<RwLock<ModbusDeviceState>> {
        self.state.clone()
    }
}

// Usage in main function
async fn run_dynamic_simulator() -> Result<(), Box<dyn std::error::Error>> {
    let simulator = DynamicSimulator::new();
    
    // Start background simulations
    simulator.start_simulations().await;
    
    // Continue with server setup using simulator.get_state()
    // ... (server code similar to previous example)
    
    Ok(())
}
```

### Configuration-Based Simulator

```rust
use serde::{Deserialize, Serialize};
use std::fs;

#[derive(Debug, Deserialize, Serialize)]
struct SimulatorConfig {
    port: u16,
    devices: Vec<DeviceConfig>,
}

#[derive(Debug, Deserialize, Serialize)]
struct DeviceConfig {
    name: String,
    unit_id: u8,
    registers: RegisterConfig,
    simulations: Vec<SimulationConfig>,
}

#[derive(Debug, Deserialize, Serialize)]
struct RegisterConfig {
    coils: usize,
    discrete_inputs: usize,
    holding_registers: usize,
    input_registers: usize,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(tag = "type")]
enum SimulationConfig {
    Temperature {
        register: usize,
        min: f32,
        max: f32,
        variance: f32,
    },
    Counter {
        register: usize,
        increment: u16,
        interval_secs: u64,
    },
    RandomBool {
        address: usize,
        probability: f32,
    },
}

fn load_config(path: &str) -> Result<SimulatorConfig, Box<dyn std::error::Error>> {
    let contents = fs::read_to_string(path)?;
    let config: SimulatorConfig = serde_json::from_str(&contents)?;
    Ok(config)
}

// Example config.json:
// {
//   "port": 502,
//   "devices": [{
//     "name": "Temperature Controller",
//     "unit_id": 1,
//     "registers": {
//       "coils": 100,
//       "discrete_inputs": 100,
//       "holding_registers": 100,
//       "input_registers": 100
//     },
//     "simulations": [
//       {
//         "type": "Temperature",
//         "register": 0,
//         "min": 20.0,
//         "max": 30.0,
//         "variance": 0.1
//       }
//     ]
//   }]
// }
```

## Summary

**Modbus Simulator Development** is crucial for modern industrial automation development workflows. Key takeaways include:

**Benefits**: Simulators eliminate hardware dependencies, reduce costs, enable parallel development, facilitate automated testing, and provide safe training environments.

**Core Components**: Effective simulators require register banks (coils, discrete inputs, holding/input registers), protocol handlers, state management, and configurable behavior patterns.

**Implementation Approaches**: Both C/C++ (using libmodbus) and Rust (using tokio-modbus) provide robust foundations for simulator development, with Rust offering superior memory safety and async capabilities.

**Advanced Features**: Production-ready simulators should include dynamic value generation (sensor simulation, counters), error injection capabilities, configuration-based setup, multi-device support, and logging/monitoring.

**Best Practices**: Use realistic timing, implement proper error handling, support both RTU and TCP protocols, provide configuration files for different scenarios, and integrate with CI/CD pipelines for automated testing.

Modbus simulators are invaluable tools that accelerate development cycles, improve code quality through comprehensive testing, and reduce deployment risks in industrial automation projects.