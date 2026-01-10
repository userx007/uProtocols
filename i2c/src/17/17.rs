use std::fmt;

/// I2C speed modes with associated specifications
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpeedMode {
    Standard,     // 100 kHz
    Fast,         // 400 kHz
    FastPlus,     // 1 MHz
    HighSpeed,    // 3.4 MHz
}

impl SpeedMode {
    /// Maximum bus capacitance in picofarads
    pub fn max_capacitance(&self) -> f32 {
        match self {
            SpeedMode::Standard => 400.0,
            SpeedMode::Fast => 400.0,
            SpeedMode::FastPlus => 550.0,
            SpeedMode::HighSpeed => 100.0,
        }
    }
    
    /// Operating frequency in Hz
    pub fn frequency(&self) -> u32 {
        match self {
            SpeedMode::Standard => 100_000,
            SpeedMode::Fast => 400_000,
            SpeedMode::FastPlus => 1_000_000,
            SpeedMode::HighSpeed => 3_400_000,
        }
    }
    
    /// Maximum rise time in nanoseconds
    pub fn max_rise_time_ns(&self) -> f32 {
        match self {
            SpeedMode::Standard | SpeedMode::Fast => 1000.0,
            SpeedMode::FastPlus => 300.0,
            SpeedMode::HighSpeed => 80.0,
        }
    }
}

impl fmt::Display for SpeedMode {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SpeedMode::Standard => write!(f, "Standard Mode (100 kHz)"),
            SpeedMode::Fast => write!(f, "Fast Mode (400 kHz)"),
            SpeedMode::FastPlus => write!(f, "Fast Mode Plus (1 MHz)"),
            SpeedMode::HighSpeed => write!(f, "High-Speed Mode (3.4 MHz)"),
        }
    }
}

/// Components contributing to total bus capacitance
#[derive(Debug, Clone)]
pub struct BusComponents {
    pub trace_length_m: f32,        // PCB trace length in meters
    pub trace_cap_per_m: f32,       // Capacitance per meter (pF/m)
    pub num_devices: u8,            // Number of I2C devices
    pub device_cap_pf: f32,         // Average device input capacitance (pF)
    pub num_connectors: u8,         // Number of connectors
    pub connector_cap_pf: f32,      // Capacitance per connector (pF)
    pub parasitic_cap_pf: f32,      // Estimated parasitic capacitance (pF)
}

impl BusComponents {
    /// Create a new bus components configuration
    pub fn new() -> Self {
        Self {
            trace_length_m: 0.0,
            trace_cap_per_m: 35.0,  // Typical PCB trace
            num_devices: 0,
            device_cap_pf: 6.0,     // Typical I2C device
            num_connectors: 0,
            connector_cap_pf: 4.0,  // Typical connector
            parasitic_cap_pf: 5.0,  // Conservative estimate
        }
    }
    
    /// Builder method: set trace length
    pub fn with_trace(mut self, length_m: f32, cap_per_m: f32) -> Self {
        self.trace_length_m = length_m;
        self.trace_cap_per_m = cap_per_m;
        self
    }
    
    /// Builder method: set devices
    pub fn with_devices(mut self, count: u8, cap_pf: f32) -> Self {
        self.num_devices = count;
        self.device_cap_pf = cap_pf;
        self
    }
    
    /// Builder method: set connectors
    pub fn with_connectors(mut self, count: u8, cap_pf: f32) -> Self {
        self.num_connectors = count;
        self.connector_cap_pf = cap_pf;
        self
    }
    
    /// Builder method: set parasitic capacitance
    pub fn with_parasitic(mut self, cap_pf: f32) -> Self {
        self.parasitic_cap_pf = cap_pf;
        self
    }
}

/// Result of capacitance analysis
#[derive(Debug)]
pub struct CapacitanceAnalysis {
    pub total_capacitance: f32,
    pub trace_contribution: f32,
    pub device_contribution: f32,
    pub connector_contribution: f32,
    pub parasitic_contribution: f32,
    pub is_valid: bool,
    pub margin_pf: f32,
    pub margin_percent: f32,
    pub min_pullup_ohms: f32,
}

/// I2C bus capacitance calculator
pub struct CapacitanceCalculator;

impl CapacitanceCalculator {
    /// Calculate total bus capacitance from components
    pub fn calculate(components: &BusComponents) -> f32 {
        let trace = components.trace_length_m * components.trace_cap_per_m;
        let devices = components.num_devices as f32 * components.device_cap_pf;
        let connectors = components.num_connectors as f32 * components.connector_cap_pf;
        
        trace + devices + connectors + components.parasitic_cap_pf
    }
    
    /// Perform full analysis for a given speed mode
    pub fn analyze(components: &BusComponents, mode: SpeedMode) -> CapacitanceAnalysis {
        let trace_contrib = components.trace_length_m * components.trace_cap_per_m;
        let device_contrib = components.num_devices as f32 * components.device_cap_pf;
        let connector_contrib = components.num_connectors as f32 * components.connector_cap_pf;
        let parasitic_contrib = components.parasitic_cap_pf;
        
        let total = trace_contrib + device_contrib + connector_contrib + parasitic_contrib;
        let max_allowed = mode.max_capacitance();
        let is_valid = total <= max_allowed;
        let margin = max_allowed - total;
        let margin_pct = (margin / max_allowed) * 100.0;
        
        let min_pullup = Self::calculate_min_pullup(total, mode);
        
        CapacitanceAnalysis {
            total_capacitance: total,
            trace_contribution: trace_contrib,
            device_contribution: device_contrib,
            connector_contribution: connector_contrib,
            parasitic_contribution: parasitic_contrib,
            is_valid,
            margin_pf: margin,
            margin_percent: margin_pct,
            min_pullup_ohms: min_pullup,
        }
    }
    
    /// Calculate minimum pull-up resistor value
    /// Formula: R_min = t_rise / (0.8473 * C_bus)
    pub fn calculate_min_pullup(bus_cap_pf: f32, mode: SpeedMode) -> f32 {
        let t_rise_s = mode.max_rise_time_ns() * 1e-9;
        let bus_cap_f = bus_cap_pf * 1e-12;
        
        t_rise_s / (0.8473 * bus_cap_f)
    }
    
    /// Calculate maximum capacitance for given pull-up resistor
    pub fn calculate_max_cap(pullup_ohms: f32, mode: SpeedMode) -> f32 {
        let t_rise_s = mode.max_rise_time_ns() * 1e-9;
        let c_max_f = t_rise_s / (0.8473 * pullup_ohms);
        
        c_max_f * 1e12  // Convert to pF
    }
    
    /// Find highest compatible speed mode for given capacitance
    pub fn find_max_speed(total_cap_pf: f32) -> Option<SpeedMode> {
        let modes = [
            SpeedMode::HighSpeed,
            SpeedMode::FastPlus,
            SpeedMode::Fast,
            SpeedMode::Standard,
        ];
        
        modes.iter()
            .find(|mode| total_cap_pf <= mode.max_capacitance())
            .copied()
    }
}

impl CapacitanceAnalysis {
    /// Print detailed report
    pub fn print_report(&self, mode: SpeedMode) {
        println!("\n========== I2C Bus Capacitance Report ==========");
        println!("Target Mode: {}", mode);
        println!();
        
        println!("Capacitance Breakdown:");
        println!("  PCB Trace:    {:6.2} pF", self.trace_contribution);
        println!("  Devices:      {:6.2} pF", self.device_contribution);
        println!("  Connectors:   {:6.2} pF", self.connector_contribution);
        println!("  Parasitic:    {:6.2} pF", self.parasitic_contribution);
        println!("  {}", "-".repeat(25));
        println!("  TOTAL:        {:6.2} pF", self.total_capacitance);
        println!();
        
        println!("Validation:");
        println!("  Maximum allowed:  {:.0} pF", mode.max_capacitance());
        println!("  Status:           {}", if self.is_valid { "PASS ✓" } else { "FAIL ✗" });
        println!("  Margin:           {:.2} pF ({:.1}%)", self.margin_pf, self.margin_percent);
        println!();
        
        println!("Pull-up Resistor:");
        println!("  Minimum required: {:.0} Ω ({:.2} kΩ)", 
                 self.min_pullup_ohms, 
                 self.min_pullup_ohms / 1000.0);
        
        // Suggest standard resistor values
        let standard_values = [1000.0, 1500.0, 2200.0, 3300.0, 4700.0, 10000.0];
        if let Some(&recommended) = standard_values.iter()
            .find(|&&val| val >= self.min_pullup_ohms) {
            println!("  Recommended:      {:.1} kΩ (standard value)", recommended / 1000.0);
        }
        
        println!("===============================================\n");
    }
}

fn main() {
    println!("I2C Bus Capacitance Analysis Tool\n");
    
    // Example 1: Compact sensor board
    let sensor_board = BusComponents::new()
        .with_trace(0.12, 35.0)      // 12 cm trace, 35 pF/m
        .with_devices(3, 5.5)        // 3 sensors, 5.5 pF each
        .with_connectors(0, 0.0)     // No connectors
        .with_parasitic(6.0);        // 6 pF parasitic
    
    let analysis = CapacitanceCalculator::analyze(&sensor_board, SpeedMode::Fast);
    analysis.print_report(SpeedMode::Fast);
    
    // Example 2: Extended system with multiple devices
    let extended_system = BusComponents::new()
        .with_trace(0.50, 40.0)      // 50 cm trace, 40 pF/m
        .with_devices(6, 7.0)        // 6 devices, 7 pF each
        .with_connectors(2, 5.0)     // 2 connectors, 5 pF each
        .with_parasitic(12.0);       // 12 pF parasitic
    
    let total_cap = CapacitanceCalculator::calculate(&extended_system);
    println!("Extended System Total Capacitance: {:.2} pF", total_cap);
    
    if let Some(max_mode) = CapacitanceCalculator::find_max_speed(total_cap) {
        println!("Maximum compatible speed: {}\n", max_mode);
        let analysis = CapacitanceCalculator::analyze(&extended_system, max_mode);
        analysis.print_report(max_mode);
    }
    
    // Example 3: Pull-up resistor analysis
    println!("Maximum Capacitance for Common Pull-up Values:");
    let pullup_values = [2200.0, 3300.0, 4700.0, 10000.0];
    
    for &pullup in &pullup_values {
        println!("\n{:.1} kΩ Pull-up:", pullup / 1000.0);
        for mode in [SpeedMode::Standard, SpeedMode::Fast, SpeedMode::FastPlus, SpeedMode::HighSpeed] {
            let max_cap = CapacitanceCalculator::calculate_max_cap(pullup, mode);
            let spec_limit = mode.max_capacitance();
            let effective = max_cap.min(spec_limit);
            println!("  {:<25} {:.1} pF (limit: {:.0} pF)", 
                     format!("{}", mode), effective, spec_limit);
        }
    }
}