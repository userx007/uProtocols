# Multi-Axis Coordination in Profibus

## Detailed Description

Multi-axis coordination in Profibus refers to the synchronized control of multiple drive systems to achieve coordinated motion in industrial automation applications. This is critical for applications like:

- **Robotics**: Coordinating multiple joint actuators for precise robotic arm movements
- **Material handling**: Synchronizing conveyors, gantries, and pick-and-place systems
- **Machine tools**: Coordinating multiple axes for CNC machining operations
- **Printing and packaging**: Maintaining registration between multiple web drives
- **Electronic assembly**: Coordinating XYZ positioning systems

### Key Concepts

**Synchronization Methods:**
- **Electronic gearing**: One axis follows another with a defined gear ratio
- **Electronic camming**: Position-based coordination using cam profiles
- **Interpolated motion**: Multiple axes follow a coordinated path trajectory
- **Time-stamped commands**: All axes execute commands at precise timestamps

**Profibus-Specific Mechanisms:**
- **Isochronous mode**: Deterministic cycle times ensure simultaneous updates
- **Sync/Freeze commands**: Global broadcast commands for coordinated start/stop
- **Cross-communication**: Drives can share position data via publisher-subscriber model
- **Master-slave coordination**: One axis acts as master, others follow as slaves

**Critical Parameters:**
- **Cycle time**: Typically 1-8ms for motion control applications
- **Synchronization accuracy**: Sub-millisecond timing precision
- **Position resolution**: Encoder counts and scaling factors
- **Velocity and acceleration limits**: Per-axis constraints

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// Profibus DP communication structures
#define MAX_AXES 4
#define PROFIBUS_CYCLE_TIME_US 2000  // 2ms cycle

// Profibus telegram structures
typedef struct {
    uint16_t control_word;
    int32_t target_position;
    int32_t target_velocity;
    uint16_t acceleration;
} ProfibusOutputData;

typedef struct {
    uint16_t status_word;
    int32_t actual_position;
    int32_t actual_velocity;
    uint16_t error_code;
} ProfibusInputData;

// Multi-axis coordination structure
typedef struct {
    int axis_id;
    ProfibusOutputData output;
    ProfibusInputData input;
    
    // Coordination parameters
    bool is_master;
    int master_axis_id;
    double gear_ratio;
    
    // Motion profile
    int32_t start_position;
    int32_t end_position;
    double max_velocity;
    double max_acceleration;
} AxisControl;

typedef struct {
    AxisControl axes[MAX_AXES];
    int num_axes;
    uint32_t sync_counter;
    bool coordinated_motion_active;
} MultiAxisController;

// Initialize multi-axis controller
void init_multi_axis_controller(MultiAxisController* controller, int num_axes) {
    controller->num_axes = num_axes;
    controller->sync_counter = 0;
    controller->coordinated_motion_active = false;
    
    for (int i = 0; i < num_axes; i++) {
        controller->axes[i].axis_id = i;
        controller->axes[i].is_master = (i == 0);
        controller->axes[i].master_axis_id = 0;
        controller->axes[i].gear_ratio = 1.0;
        
        // Initialize control word (enable, no halt)
        controller->axes[i].output.control_word = 0x000F;
    }
}

// Electronic gearing: slave follows master with gear ratio
void apply_electronic_gearing(MultiAxisController* controller) {
    AxisControl* master = &controller->axes[0];
    
    for (int i = 1; i < controller->num_axes; i++) {
        AxisControl* slave = &controller->axes[i];
        
        if (!slave->is_master && slave->master_axis_id == master->axis_id) {
            // Calculate slave position based on master position and gear ratio
            int32_t master_pos = master->input.actual_position;
            int32_t slave_target = (int32_t)(master_pos * slave->gear_ratio);
            
            slave->output.target_position = slave_target;
            
            // Scale velocity as well
            int32_t master_vel = master->input.actual_velocity;
            slave->output.target_velocity = (int32_t)(master_vel * slave->gear_ratio);
        }
    }
}

// Coordinated linear interpolation for multi-axis motion
void coordinated_linear_move(MultiAxisController* controller,
                             int32_t target_positions[],
                             double max_velocity) {
    // Calculate motion parameters
    double max_distance = 0.0;
    
    for (int i = 0; i < controller->num_axes; i++) {
        AxisControl* axis = &controller->axes[i];
        axis->start_position = axis->input.actual_position;
        axis->end_position = target_positions[i];
        
        double distance = fabs(axis->end_position - axis->start_position);
        if (distance > max_distance) {
            max_distance = distance;
        }
    }
    
    // Calculate motion time based on limiting axis
    double motion_time = max_distance / max_velocity;
    
    // Set coordinated velocities for all axes
    for (int i = 0; i < controller->num_axes; i++) {
        AxisControl* axis = &controller->axes[i];
        double axis_distance = axis->end_position - axis->start_position;
        axis->max_velocity = axis_distance / motion_time;
        
        axis->output.target_position = axis->end_position;
        axis->output.target_velocity = (int32_t)fabs(axis->max_velocity);
    }
    
    controller->coordinated_motion_active = true;
}

// Send sync command to all axes (Profibus global control)
void send_sync_command(MultiAxisController* controller, uint8_t sync_type) {
    // In real Profibus, this would use SYNC or FREEZE telegrams
    // sync_type: 0=SYNC (latch outputs), 1=FREEZE (latch inputs)
    
    for (int i = 0; i < controller->num_axes; i++) {
        if (sync_type == 0) {
            // SYNC: All drives update outputs simultaneously
            controller->axes[i].output.control_word |= 0x0010;
        } else {
            // FREEZE: All drives latch input data simultaneously
            controller->axes[i].output.control_word |= 0x0020;
        }
    }
    
    controller->sync_counter++;
}

// Cyclic Profibus communication (called every cycle)
void profibus_cyclic_task(MultiAxisController* controller) {
    // 1. Read input data from all drives (simulated)
    for (int i = 0; i < controller->num_axes; i++) {
        // In real implementation, this would read from Profibus DP
        // profibus_read_input(axis_id, &controller->axes[i].input);
    }
    
    // 2. Apply coordination logic
    if (controller->coordinated_motion_active) {
        apply_electronic_gearing(controller);
    }
    
    // 3. Send output data to all drives (simulated)
    for (int i = 0; i < controller->num_axes; i++) {
        // In real implementation, this would write to Profibus DP
        // profibus_write_output(axis_id, &controller->axes[i].output);
    }
    
    // 4. Send sync command for simultaneous execution
    send_sync_command(controller, 0);  // SYNC command
}

// Example: Circular interpolation for 2-axis system
void circular_interpolation_2axis(MultiAxisController* controller,
                                  double center_x, double center_y,
                                  double radius, double angular_velocity) {
    static double angle = 0.0;
    const double dt = PROFIBUS_CYCLE_TIME_US / 1000000.0;  // Convert to seconds
    
    // Calculate positions on circle
    double x = center_x + radius * cos(angle);
    double y = center_y + radius * sin(angle);
    
    // Update axis positions
    controller->axes[0].output.target_position = (int32_t)(x * 1000);  // mm to encoder counts
    controller->axes[1].output.target_position = (int32_t)(y * 1000);
    
    // Calculate velocities
    double vx = -radius * angular_velocity * sin(angle);
    double vy = radius * angular_velocity * cos(angle);
    
    controller->axes[0].output.target_velocity = (int32_t)(vx * 1000);
    controller->axes[1].output.target_velocity = (int32_t)(vy * 1000);
    
    // Increment angle
    angle += angular_velocity * dt;
    if (angle >= 2 * M_PI) angle -= 2 * M_PI;
}

// Main example
int main() {
    MultiAxisController controller;
    init_multi_axis_controller(&controller, 3);
    
    // Configure electronic gearing: Axis 1 and 2 follow Axis 0
    controller.axes[1].gear_ratio = 2.0;  // 2:1 gear ratio
    controller.axes[2].gear_ratio = 0.5;  // 1:2 gear ratio
    
    printf("Multi-Axis Coordination Example\n");
    printf("================================\n\n");
    
    // Example 1: Coordinated linear move
    int32_t targets[] = {10000, 20000, 5000};
    coordinated_linear_move(&controller, targets, 5000.0);
    
    printf("Coordinated move to positions: [%d, %d, %d]\n",
           targets[0], targets[1], targets[2]);
    
    // Simulate cyclic execution
    for (int cycle = 0; cycle < 10; cycle++) {
        profibus_cyclic_task(&controller);
        printf("Cycle %d: Sync counter = %u\n", cycle, controller.sync_counter);
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::f64::consts::PI;

const MAX_AXES: usize = 4;
const PROFIBUS_CYCLE_TIME_US: u32 = 2000; // 2ms cycle

// Profibus telegram structures
#[derive(Debug, Clone, Copy)]
struct ProfibusOutputData {
    control_word: u16,
    target_position: i32,
    target_velocity: i32,
    acceleration: u16,
}

#[derive(Debug, Clone, Copy)]
struct ProfibusInputData {
    status_word: u16,
    actual_position: i32,
    actual_velocity: i32,
    error_code: u16,
}

// Axis control structure
#[derive(Debug, Clone)]
struct AxisControl {
    axis_id: usize,
    output: ProfibusOutputData,
    input: ProfibusInputData,
    
    // Coordination parameters
    is_master: bool,
    master_axis_id: usize,
    gear_ratio: f64,
    
    // Motion profile
    start_position: i32,
    end_position: i32,
    max_velocity: f64,
    max_acceleration: f64,
}

impl AxisControl {
    fn new(axis_id: usize) -> Self {
        Self {
            axis_id,
            output: ProfibusOutputData {
                control_word: 0x000F, // Enable, no halt
                target_position: 0,
                target_velocity: 0,
                acceleration: 1000,
            },
            input: ProfibusInputData {
                status_word: 0,
                actual_position: 0,
                actual_velocity: 0,
                error_code: 0,
            },
            is_master: axis_id == 0,
            master_axis_id: 0,
            gear_ratio: 1.0,
            start_position: 0,
            end_position: 0,
            max_velocity: 5000.0,
            max_acceleration: 10000.0,
        }
    }
}

// Multi-axis controller
struct MultiAxisController {
    axes: Vec<AxisControl>,
    sync_counter: u32,
    coordinated_motion_active: bool,
}

impl MultiAxisController {
    fn new(num_axes: usize) -> Self {
        let axes = (0..num_axes)
            .map(|i| AxisControl::new(i))
            .collect();
        
        Self {
            axes,
            sync_counter: 0,
            coordinated_motion_active: false,
        }
    }
    
    // Electronic gearing: slave follows master with gear ratio
    fn apply_electronic_gearing(&mut self) {
        let master_pos = self.axes[0].input.actual_position;
        let master_vel = self.axes[0].input.actual_velocity;
        
        for i in 1..self.axes.len() {
            if !self.axes[i].is_master && self.axes[i].master_axis_id == 0 {
                let gear_ratio = self.axes[i].gear_ratio;
                
                self.axes[i].output.target_position = 
                    (master_pos as f64 * gear_ratio) as i32;
                self.axes[i].output.target_velocity = 
                    (master_vel as f64 * gear_ratio) as i32;
            }
        }
    }
    
    // Coordinated linear interpolation
    fn coordinated_linear_move(&mut self, target_positions: &[i32], max_velocity: f64) {
        // Calculate motion parameters
        let mut max_distance = 0.0;
        
        for (i, axis) in self.axes.iter_mut().enumerate() {
            axis.start_position = axis.input.actual_position;
            axis.end_position = target_positions[i];
            
            let distance = (axis.end_position - axis.start_position).abs() as f64;
            if distance > max_distance {
                max_distance = distance;
            }
        }
        
        // Calculate motion time based on limiting axis
        let motion_time = max_distance / max_velocity;
        
        // Set coordinated velocities for all axes
        for (i, axis) in self.axes.iter_mut().enumerate() {
            let axis_distance = (axis.end_position - axis.start_position) as f64;
            axis.max_velocity = axis_distance / motion_time;
            
            axis.output.target_position = axis.end_position;
            axis.output.target_velocity = axis.max_velocity.abs() as i32;
        }
        
        self.coordinated_motion_active = true;
    }
    
    // Send sync command to all axes
    fn send_sync_command(&mut self, sync_type: u8) {
        // sync_type: 0=SYNC (latch outputs), 1=FREEZE (latch inputs)
        for axis in &mut self.axes {
            if sync_type == 0 {
                axis.output.control_word |= 0x0010; // SYNC
            } else {
                axis.output.control_word |= 0x0020; // FREEZE
            }
        }
        
        self.sync_counter += 1;
    }
    
    // Cyclic Profibus communication
    fn profibus_cyclic_task(&mut self) {
        // 1. Read input data from all drives (simulated)
        for axis in &mut self.axes {
            // In real implementation: profibus_read_input(axis.axis_id, &mut axis.input);
        }
        
        // 2. Apply coordination logic
        if self.coordinated_motion_active {
            self.apply_electronic_gearing();
        }
        
        // 3. Send output data to all drives (simulated)
        for axis in &self.axes {
            // In real implementation: profibus_write_output(axis.axis_id, &axis.output);
        }
        
        // 4. Send sync command for simultaneous execution
        self.send_sync_command(0); // SYNC command
    }
    
    // Circular interpolation for 2-axis system
    fn circular_interpolation_2axis(
        &mut self,
        center_x: f64,
        center_y: f64,
        radius: f64,
        angular_velocity: f64,
        angle: &mut f64,
    ) {
        let dt = PROFIBUS_CYCLE_TIME_US as f64 / 1_000_000.0; // Convert to seconds
        
        // Calculate positions on circle
        let x = center_x + radius * angle.cos();
        let y = center_y + radius * angle.sin();
        
        // Update axis positions
        self.axes[0].output.target_position = (x * 1000.0) as i32;
        self.axes[1].output.target_position = (y * 1000.0) as i32;
        
        // Calculate velocities
        let vx = -radius * angular_velocity * angle.sin();
        let vy = radius * angular_velocity * angle.cos();
        
        self.axes[0].output.target_velocity = (vx * 1000.0) as i32;
        self.axes[1].output.target_velocity = (vy * 1000.0) as i32;
        
        // Increment angle
        *angle += angular_velocity * dt;
        if *angle >= 2.0 * PI {
            *angle -= 2.0 * PI;
        }
    }
}

fn main() {
    let mut controller = MultiAxisController::new(3);
    
    // Configure electronic gearing
    controller.axes[1].gear_ratio = 2.0; // 2:1 gear ratio
    controller.axes[2].gear_ratio = 0.5; // 1:2 gear ratio
    
    println!("Multi-Axis Coordination Example");
    println!("================================\n");
    
    // Example 1: Coordinated linear move
    let targets = vec![10000, 20000, 5000];
    controller.coordinated_linear_move(&targets, 5000.0);
    
    println!("Coordinated move to positions: {:?}", targets);
    
    // Simulate cyclic execution
    for cycle in 0..10 {
        controller.profibus_cyclic_task();
        println!("Cycle {}: Sync counter = {}", cycle, controller.sync_counter);
    }
    
    // Example 2: Circular interpolation
    println!("\nCircular interpolation example:");
    let mut angle = 0.0;
    for cycle in 0..5 {
        controller.circular_interpolation_2axis(0.0, 0.0, 100.0, 0.5, &mut angle);
        println!("Cycle {}: X={}, Y={}", 
                 cycle,
                 controller.axes[0].output.target_position,
                 controller.axes[1].output.target_position);
    }
}
```

## Summary

Multi-axis coordination in Profibus enables precise synchronization of multiple drive systems for complex motion applications. Key implementation aspects include:

**Technical Requirements:**
- Isochronous communication mode with deterministic cycle times (1-8ms typical)
- SYNC/FREEZE global control commands for simultaneous updates
- High-resolution position feedback and interpolation algorithms

**Coordination Strategies:**
- **Electronic gearing** for master-slave relationships with configurable gear ratios
- **Interpolated motion** for coordinated path following (linear, circular, spline)
- **Time-synchronized commands** ensuring all axes execute movements simultaneously

**Implementation Considerations:**
- Proper cycle time configuration balancing responsiveness and bus load
- Position scaling and coordinate transformation for different mechanical systems
- Error handling for axis synchronization loss and position tracking errors
- Real-time requirements necessitating deterministic scheduling

Both C/C++ and Rust implementations demonstrate electronic gearing, coordinated linear moves, and circular interpolation, providing robust frameworks for multi-axis motion control in industrial automation systems.