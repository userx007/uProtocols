# Profibus Batch Control Integration - ISA-88 Implementation

## Overview

Batch Control Integration involves implementing the ISA-88 (ANSI/ISA-88) standard for batch process control using Profibus-connected field devices and equipment. ISA-88 provides a standardized approach to batch manufacturing, defining procedural control models, equipment control models, and recipe structures. When integrated with Profibus, it enables consistent, repeatable, and traceable batch processes across distributed automation systems.

## ISA-88 Key Concepts

### Physical Model
- **Enterprise** → **Site** → **Area** → **Process Cell** → **Unit** → **Equipment Module** → **Control Module**

### Procedural Control Model
- **Procedure**: Complete manufacturing sequence
- **Unit Procedure**: Operations on a single unit
- **Operation**: Major processing step
- **Phase**: Basic control task (the command level for equipment)

### Recipe Structure
- **General Recipe**: Product-independent template
- **Site Recipe**: Site-specific adaptation
- **Master Recipe**: Approved production recipe
- **Control Recipe**: Instance for a specific batch

## Profibus Integration Architecture

Profibus connects the physical equipment (Process Cell and below) to the batch control system through:
- **DP (Decentralized Periphery)**: Fast cyclic data exchange for real-time control
- **PA (Process Automation)**: Intrinsically safe field devices in hazardous areas
- **FMS (Fieldbus Message Specification)**: Acyclic communication for parameters and diagnostics

## C/C++ Implementation

### Profibus Batch Phase Controller

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

// ISA-88 Phase States
typedef enum {
    PHASE_IDLE = 0,
    PHASE_RUNNING = 1,
    PHASE_COMPLETE = 2,
    PHASE_PAUSED = 3,
    PHASE_HOLDING = 4,
    PHASE_STOPPED = 5,
    PHASE_ABORTING = 6,
    PHASE_ABORTED = 7
} PhaseState;

// ISA-88 Phase Commands
typedef enum {
    CMD_NONE = 0,
    CMD_START = 1,
    CMD_PAUSE = 2,
    CMD_RESUME = 3,
    CMD_STOP = 4,
    CMD_ABORT = 5,
    CMD_RESET = 6
} PhaseCommand;

// Profibus Device Address
typedef struct {
    uint8_t station_addr;
    uint16_t slot;
    uint16_t subslot;
} ProfibusAddr;

// Phase Parameters (Recipe Parameters)
typedef struct {
    double setpoint;
    double tolerance;
    double duration_seconds;
    double ramp_rate;
} PhaseParameters;

// Batch Phase Context
typedef struct {
    char phase_name[64];
    PhaseState state;
    PhaseCommand current_command;
    PhaseParameters params;
    ProfibusAddr device_addr;
    
    // Runtime data
    time_t start_time;
    double current_value;
    bool conditions_met;
    char error_message[256];
} BatchPhase;

// Profibus Communication Functions (simplified)
int profibus_write_output(ProfibusAddr addr, uint8_t *data, size_t len) {
    // Actual implementation would use Profibus DP API
    printf("[Profibus] Writing %zu bytes to Station %d, Slot %d\n", 
           len, addr.station_addr, addr.slot);
    return 0;
}

int profibus_read_input(ProfibusAddr addr, uint8_t *data, size_t len) {
    // Actual implementation would use Profibus DP API
    printf("[Profibus] Reading %zu bytes from Station %d, Slot %d\n", 
           len, addr.station_addr, addr.slot);
    return 0;
}

// Initialize a batch phase
void batch_phase_init(BatchPhase *phase, const char *name, ProfibusAddr addr) {
    strncpy(phase->phase_name, name, sizeof(phase->phase_name) - 1);
    phase->state = PHASE_IDLE;
    phase->current_command = CMD_NONE;
    phase->device_addr = addr;
    phase->start_time = 0;
    phase->current_value = 0.0;
    phase->conditions_met = false;
    memset(phase->error_message, 0, sizeof(phase->error_message));
}

// Set phase parameters (from recipe)
void batch_phase_set_parameters(BatchPhase *phase, PhaseParameters params) {
    phase->params = params;
}

// Execute phase command
bool batch_phase_execute_command(BatchPhase *phase, PhaseCommand cmd) {
    switch (cmd) {
        case CMD_START:
            if (phase->state == PHASE_IDLE) {
                phase->state = PHASE_RUNNING;
                phase->start_time = time(NULL);
                phase->current_command = cmd;
                printf("[%s] Phase STARTED\n", phase->phase_name);
                return true;
            }
            break;
            
        case CMD_PAUSE:
            if (phase->state == PHASE_RUNNING) {
                phase->state = PHASE_PAUSED;
                phase->current_command = cmd;
                printf("[%s] Phase PAUSED\n", phase->phase_name);
                return true;
            }
            break;
            
        case CMD_RESUME:
            if (phase->state == PHASE_PAUSED) {
                phase->state = PHASE_RUNNING;
                phase->current_command = cmd;
                printf("[%s] Phase RESUMED\n", phase->phase_name);
                return true;
            }
            break;
            
        case CMD_STOP:
            if (phase->state == PHASE_RUNNING || phase->state == PHASE_PAUSED) {
                phase->state = PHASE_STOPPED;
                phase->current_command = cmd;
                printf("[%s] Phase STOPPED\n", phase->phase_name);
                return true;
            }
            break;
            
        case CMD_ABORT:
            phase->state = PHASE_ABORTING;
            phase->current_command = cmd;
            printf("[%s] Phase ABORTING\n", phase->phase_name);
            return true;
            
        case CMD_RESET:
            if (phase->state == PHASE_COMPLETE || 
                phase->state == PHASE_STOPPED || 
                phase->state == PHASE_ABORTED) {
                phase->state = PHASE_IDLE;
                phase->current_command = CMD_NONE;
                printf("[%s] Phase RESET\n", phase->phase_name);
                return true;
            }
            break;
            
        default:
            break;
    }
    
    printf("[%s] Invalid command %d for state %d\n", 
           phase->phase_name, cmd, phase->state);
    return false;
}

// Phase execution logic
void batch_phase_execute(BatchPhase *phase) {
    uint8_t output_data[8];
    uint8_t input_data[8];
    
    if (phase->state != PHASE_RUNNING) {
        return;
    }
    
    // Read current process value from Profibus device
    profibus_read_input(phase->device_addr, input_data, sizeof(input_data));
    
    // Parse process value (example: temperature as IEEE 754 float)
    memcpy(&phase->current_value, input_data, sizeof(float));
    
    // Write setpoint to Profibus device
    float setpoint_float = (float)phase->params.setpoint;
    memcpy(output_data, &setpoint_float, sizeof(float));
    profibus_write_output(phase->device_addr, output_data, sizeof(float));
    
    // Check completion conditions
    double elapsed = difftime(time(NULL), phase->start_time);
    double error = fabs(phase->current_value - phase->params.setpoint);
    
    // Phase complete if within tolerance for required duration
    if (error <= phase->params.tolerance && 
        elapsed >= phase->params.duration_seconds) {
        phase->conditions_met = true;
        phase->state = PHASE_COMPLETE;
        printf("[%s] Phase COMPLETE (Value: %.2f, Setpoint: %.2f)\n", 
               phase->phase_name, phase->current_value, phase->params.setpoint);
    }
    
    // Log progress
    if ((int)elapsed % 5 == 0) {  // Every 5 seconds
        printf("[%s] Running: %.0fs elapsed, Value: %.2f, Setpoint: %.2f\n",
               phase->phase_name, elapsed, phase->current_value, 
               phase->params.setpoint);
    }
}

// Batch Recipe Structure
typedef struct {
    char recipe_name[64];
    char product_code[32];
    int num_phases;
    BatchPhase phases[16];  // Maximum 16 phases per recipe
} BatchRecipe;

// Execute complete batch recipe
void batch_recipe_execute(BatchRecipe *recipe) {
    printf("\n=== Starting Batch Recipe: %s ===\n", recipe->recipe_name);
    printf("Product Code: %s\n", recipe->product_code);
    printf("Total Phases: %d\n\n", recipe->num_phases);
    
    for (int i = 0; i < recipe->num_phases; i++) {
        BatchPhase *phase = &recipe->phases[i];
        
        // Start the phase
        batch_phase_execute_command(phase, CMD_START);
        
        // Execute phase until complete
        while (phase->state == PHASE_RUNNING) {
            batch_phase_execute(phase);
            // In real implementation, this would be called from cyclic task
            // Here we simulate with a sleep
            // sleep(1);
            
            // Simulate completion for demo
            phase->state = PHASE_COMPLETE;
        }
        
        if (phase->state == PHASE_COMPLETE) {
            printf("[%s] Phase completed successfully\n\n", phase->phase_name);
        } else {
            printf("[%s] Phase failed with state %d\n\n", 
                   phase->phase_name, phase->state);
            break;
        }
    }
    
    printf("=== Batch Recipe Complete ===\n\n");
}

// Example usage
int main() {
    BatchRecipe recipe;
    
    // Initialize recipe
    strncpy(recipe.recipe_name, "Chemical_Reaction_A", sizeof(recipe.recipe_name));
    strncpy(recipe.product_code, "PROD-12345", sizeof(recipe.product_code));
    recipe.num_phases = 3;
    
    // Phase 1: Charge Material A
    ProfibusAddr valve_addr = {.station_addr = 3, .slot = 1, .subslot = 0};
    batch_phase_init(&recipe.phases[0], "Charge_Material_A", valve_addr);
    PhaseParameters charge_params = {
        .setpoint = 1.0,        // Valve open
        .tolerance = 0.1,
        .duration_seconds = 30.0,
        .ramp_rate = 0.0
    };
    batch_phase_set_parameters(&recipe.phases[0], charge_params);
    
    // Phase 2: Heat to Reaction Temperature
    ProfibusAddr heater_addr = {.station_addr = 4, .slot = 2, .subslot = 0};
    batch_phase_init(&recipe.phases[1], "Heat_To_Reaction_Temp", heater_addr);
    PhaseParameters heat_params = {
        .setpoint = 75.0,       // 75°C
        .tolerance = 1.0,
        .duration_seconds = 60.0,
        .ramp_rate = 1.0        // 1°C per second
    };
    batch_phase_set_parameters(&recipe.phases[1], heat_params);
    
    // Phase 3: Hold Reaction Time
    batch_phase_init(&recipe.phases[2], "Hold_Reaction", heater_addr);
    PhaseParameters hold_params = {
        .setpoint = 75.0,       // Maintain 75°C
        .tolerance = 0.5,
        .duration_seconds = 120.0,
        .ramp_rate = 0.0
    };
    batch_phase_set_parameters(&recipe.phases[2], hold_params);
    
    // Execute batch
    batch_recipe_execute(&recipe);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};
use std::fmt;

// ISA-88 Phase States
#[derive(Debug, Clone, Copy, PartialEq)]
enum PhaseState {
    Idle,
    Running,
    Complete,
    Paused,
    Holding,
    Stopped,
    Aborting,
    Aborted,
}

impl fmt::Display for PhaseState {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

// ISA-88 Phase Commands
#[derive(Debug, Clone, Copy)]
enum PhaseCommand {
    None,
    Start,
    Pause,
    Resume,
    Stop,
    Abort,
    Reset,
}

// Profibus Device Address
#[derive(Debug, Clone, Copy)]
struct ProfibusAddr {
    station_addr: u8,
    slot: u16,
    subslot: u16,
}

// Phase Parameters (Recipe Parameters)
#[derive(Debug, Clone, Copy)]
struct PhaseParameters {
    setpoint: f64,
    tolerance: f64,
    duration_seconds: f64,
    ramp_rate: f64,
}

// Batch Phase Context
struct BatchPhase {
    phase_name: String,
    state: PhaseState,
    current_command: PhaseCommand,
    params: PhaseParameters,
    device_addr: ProfibusAddr,
    
    // Runtime data
    start_time: Option<Instant>,
    current_value: f64,
    conditions_met: bool,
    error_message: String,
}

impl BatchPhase {
    fn new(name: &str, addr: ProfibusAddr) -> Self {
        BatchPhase {
            phase_name: name.to_string(),
            state: PhaseState::Idle,
            current_command: PhaseCommand::None,
            params: PhaseParameters {
                setpoint: 0.0,
                tolerance: 0.0,
                duration_seconds: 0.0,
                ramp_rate: 0.0,
            },
            device_addr: addr,
            start_time: None,
            current_value: 0.0,
            conditions_met: false,
            error_message: String::new(),
        }
    }
    
    fn set_parameters(&mut self, params: PhaseParameters) {
        self.params = params;
    }
    
    fn execute_command(&mut self, cmd: PhaseCommand) -> Result<(), String> {
        match cmd {
            PhaseCommand::Start => {
                if self.state == PhaseState::Idle {
                    self.state = PhaseState::Running;
                    self.start_time = Some(Instant::now());
                    self.current_command = cmd;
                    println!("[{}] Phase STARTED", self.phase_name);
                    Ok(())
                } else {
                    Err(format!("Cannot start from state {}", self.state))
                }
            }
            
            PhaseCommand::Pause => {
                if self.state == PhaseState::Running {
                    self.state = PhaseState::Paused;
                    self.current_command = cmd;
                    println!("[{}] Phase PAUSED", self.phase_name);
                    Ok(())
                } else {
                    Err(format!("Cannot pause from state {}", self.state))
                }
            }
            
            PhaseCommand::Resume => {
                if self.state == PhaseState::Paused {
                    self.state = PhaseState::Running;
                    self.current_command = cmd;
                    println!("[{}] Phase RESUMED", self.phase_name);
                    Ok(())
                } else {
                    Err(format!("Cannot resume from state {}", self.state))
                }
            }
            
            PhaseCommand::Stop => {
                if self.state == PhaseState::Running || self.state == PhaseState::Paused {
                    self.state = PhaseState::Stopped;
                    self.current_command = cmd;
                    println!("[{}] Phase STOPPED", self.phase_name);
                    Ok(())
                } else {
                    Err(format!("Cannot stop from state {}", self.state))
                }
            }
            
            PhaseCommand::Abort => {
                self.state = PhaseState::Aborting;
                self.current_command = cmd;
                println!("[{}] Phase ABORTING", self.phase_name);
                Ok(())
            }
            
            PhaseCommand::Reset => {
                if matches!(self.state, PhaseState::Complete | PhaseState::Stopped | PhaseState::Aborted) {
                    self.state = PhaseState::Idle;
                    self.current_command = PhaseCommand::None;
                    self.start_time = None;
                    println!("[{}] Phase RESET", self.phase_name);
                    Ok(())
                } else {
                    Err(format!("Cannot reset from state {}", self.state))
                }
            }
            
            PhaseCommand::None => Ok(()),
        }
    }
    
    fn execute(&mut self) {
        if self.state != PhaseState::Running {
            return;
        }
        
        // Read current process value from Profibus
        self.current_value = profibus_read_process_value(self.device_addr);
        
        // Write setpoint to Profibus
        profibus_write_setpoint(self.device_addr, self.params.setpoint);
        
        // Check completion conditions
        if let Some(start) = self.start_time {
            let elapsed = start.elapsed().as_secs_f64();
            let error = (self.current_value - self.params.setpoint).abs();
            
            // Phase complete if within tolerance for required duration
            if error <= self.params.tolerance && elapsed >= self.params.duration_seconds {
                self.conditions_met = true;
                self.state = PhaseState::Complete;
                println!(
                    "[{}] Phase COMPLETE (Value: {:.2}, Setpoint: {:.2})",
                    self.phase_name, self.current_value, self.params.setpoint
                );
            }
            
            // Log progress every 5 seconds
            if elapsed as u64 % 5 == 0 {
                println!(
                    "[{}] Running: {:.0}s elapsed, Value: {:.2}, Setpoint: {:.2}",
                    self.phase_name, elapsed, self.current_value, self.params.setpoint
                );
            }
        }
    }
}

// Profibus Communication Functions (simplified)
fn profibus_read_process_value(addr: ProfibusAddr) -> f64 {
    println!(
        "[Profibus] Reading from Station {}, Slot {}",
        addr.station_addr, addr.slot
    );
    // Simulate reading a temperature value
    75.0
}

fn profibus_write_setpoint(addr: ProfibusAddr, value: f64) {
    println!(
        "[Profibus] Writing setpoint {:.2} to Station {}, Slot {}",
        value, addr.station_addr, addr.slot
    );
}

// Batch Recipe Structure
struct BatchRecipe {
    recipe_name: String,
    product_code: String,
    phases: Vec<BatchPhase>,
}

impl BatchRecipe {
    fn new(name: &str, product: &str) -> Self {
        BatchRecipe {
            recipe_name: name.to_string(),
            product_code: product.to_string(),
            phases: Vec::new(),
        }
    }
    
    fn add_phase(&mut self, phase: BatchPhase) {
        self.phases.push(phase);
    }
    
    fn execute(&mut self) -> Result<(), String> {
        println!("\n=== Starting Batch Recipe: {} ===", self.recipe_name);
        println!("Product Code: {}", self.product_code);
        println!("Total Phases: {}\n", self.phases.len());
        
        for phase in &mut self.phases {
            // Start the phase
            phase.execute_command(PhaseCommand::Start)?;
            
            // Execute phase until complete
            while phase.state == PhaseState::Running {
                phase.execute();
                
                // In real implementation, this would be called from cyclic task
                std::thread::sleep(Duration::from_millis(100));
                
                // Simulate completion for demo
                phase.state = PhaseState::Complete;
            }
            
            if phase.state == PhaseState::Complete {
                println!("[{}] Phase completed successfully\n", phase.phase_name);
            } else {
                return Err(format!(
                    "[{}] Phase failed with state {}",
                    phase.phase_name, phase.state
                ));
            }
        }
        
        println!("=== Batch Recipe Complete ===\n");
        Ok(())
    }
}

// Equipment Module trait for ISA-88 equipment model
trait EquipmentModule {
    fn get_status(&self) -> String;
    fn acquire(&mut self) -> Result<(), String>;
    fn release(&mut self) -> Result<(), String>;
}

// Example: Reactor Equipment Module
struct ReactorModule {
    name: String,
    profibus_addr: ProfibusAddr,
    acquired: bool,
}

impl ReactorModule {
    fn new(name: &str, addr: ProfibusAddr) -> Self {
        ReactorModule {
            name: name.to_string(),
            profibus_addr: addr,
            acquired: false,
        }
    }
}

impl EquipmentModule for ReactorModule {
    fn get_status(&self) -> String {
        format!("{}: {}", self.name, if self.acquired { "Acquired" } else { "Available" })
    }
    
    fn acquire(&mut self) -> Result<(), String> {
        if self.acquired {
            Err(format!("{} is already acquired", self.name))
        } else {
            self.acquired = true;
            println!("[Equipment] {} acquired", self.name);
            Ok(())
        }
    }
    
    fn release(&mut self) -> Result<(), String> {
        if !self.acquired {
            Err(format!("{} is not acquired", self.name))
        } else {
            self.acquired = false;
            println!("[Equipment] {} released", self.name);
            Ok(())
        }
    }
}

// Example usage
fn main() {
    // Create batch recipe
    let mut recipe = BatchRecipe::new("Chemical_Reaction_A", "PROD-12345");
    
    // Phase 1: Charge Material A
    let valve_addr = ProfibusAddr {
        station_addr: 3,
        slot: 1,
        subslot: 0,
    };
    let mut charge_phase = BatchPhase::new("Charge_Material_A", valve_addr);
    charge_phase.set_parameters(PhaseParameters {
        setpoint: 1.0,  // Valve open
        tolerance: 0.1,
        duration_seconds: 30.0,
        ramp_rate: 0.0,
    });
    recipe.add_phase(charge_phase);
    
    // Phase 2: Heat to Reaction Temperature
    let heater_addr = ProfibusAddr {
        station_addr: 4,
        slot: 2,
        subslot: 0,
    };
    let mut heat_phase = BatchPhase::new("Heat_To_Reaction_Temp", heater_addr);
    heat_phase.set_parameters(PhaseParameters {
        setpoint: 75.0,  // 75°C
        tolerance: 1.0,
        duration_seconds: 60.0,
        ramp_rate: 1.0,  // 1°C per second
    });
    recipe.add_phase(heat_phase);
    
    // Phase 3: Hold Reaction Time
    let mut hold_phase = BatchPhase::new("Hold_Reaction", heater_addr);
    hold_phase.set_parameters(PhaseParameters {
        setpoint: 75.0,  // Maintain 75°C
        tolerance: 0.5,
        duration_seconds: 120.0,
        ramp_rate: 0.0,
    });
    recipe.add_phase(hold_phase);
    
    // Execute batch
    if let Err(e) = recipe.execute() {
        eprintln!("Batch execution failed: {}", e);
    }
    
    // Demonstrate equipment module
    println!("\n=== Equipment Module Example ===");
    let reactor_addr = ProfibusAddr {
        station_addr: 5,
        slot: 1,
        subslot: 0,
    };
    let mut reactor = ReactorModule::new("Reactor_R101", reactor_addr);
    
    println!("{}", reactor.get_status());
    reactor.acquire().unwrap();
    println!("{}", reactor.get_status());
    reactor.release().unwrap();
    println!("{}", reactor.get_status());
}
```

## Summary

**Batch Control Integration with Profibus and ISA-88** provides a standardized framework for implementing repeatable, traceable batch manufacturing processes. The ISA-88 standard defines clear hierarchical models for equipment (from enterprise down to control modules) and procedures (from complete procedures down to phases), while Profibus enables reliable real-time communication with field devices.

**Key Benefits:**
- **Standardization**: Common terminology and models across industries
- **Flexibility**: Recipe-based approach allows product changeovers without reprogramming
- **Traceability**: Complete batch genealogy and material tracking
- **Reusability**: Phases and equipment modules can be reused across recipes
- **Scalability**: Hierarchical structure supports plant-wide implementations

**Implementation Considerations:**
- **Equipment Arbitration**: Manage resource allocation when multiple batches compete for equipment
- **Exception Handling**: Implement robust error recovery and abort procedures
- **Recipe Management**: Version control and approval workflows for recipes
- **Data Logging**: Comprehensive batch records for regulatory compliance (e.g., FDA 21 CFR Part 11)
- **Real-time Performance**: Balance cyclic Profibus communication with batch logic execution

The code examples demonstrate phase state management, recipe execution, Profibus device integration, and equipment module abstraction, providing a foundation for building complete ISA-88 compliant batch control systems with Profibus connectivity.