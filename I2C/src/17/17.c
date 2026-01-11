#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// I2C speed mode definitions
typedef enum {
    I2C_STANDARD_MODE = 0,    // 100 kHz
    I2C_FAST_MODE,            // 400 kHz
    I2C_FAST_MODE_PLUS,       // 1 MHz
    I2C_HIGH_SPEED_MODE       // 3.4 MHz
} i2c_speed_mode_t;

// Capacitance limits (in pF)
static const uint16_t I2C_CAP_LIMITS[] = {
    400,  // Standard Mode
    400,  // Fast Mode
    550,  // Fast Mode Plus
    100   // High-Speed Mode
};

// Speed frequencies (in Hz)
static const uint32_t I2C_FREQUENCIES[] = {
    100000,   // Standard Mode
    400000,   // Fast Mode
    1000000,  // Fast Mode Plus
    3400000   // High-Speed Mode
};

// Structure to hold bus capacitance components
typedef struct {
    float trace_length_m;        // PCB trace length in meters
    float trace_cap_per_m;       // Capacitance per meter (pF/m)
    uint8_t num_devices;         // Number of I2C devices
    float device_cap_pf;         // Average device input capacitance (pF)
    uint8_t num_connectors;      // Number of connectors
    float connector_cap_pf;      // Capacitance per connector (pF)
    float parasitic_cap_pf;      // Estimated parasitic capacitance (pF)
} i2c_bus_components_t;

// Calculate total bus capacitance
float calculate_bus_capacitance(const i2c_bus_components_t* components) {
    float trace_cap = components->trace_length_m * components->trace_cap_per_m;
    float device_cap = components->num_devices * components->device_cap_pf;
    float connector_cap = components->num_connectors * components->connector_cap_pf;
    
    return trace_cap + device_cap + connector_cap + components->parasitic_cap_pf;
}

// Validate if capacitance is within limits for given mode
bool validate_capacitance(float total_cap_pf, i2c_speed_mode_t mode) {
    return total_cap_pf <= I2C_CAP_LIMITS[mode];
}

// Calculate required pull-up resistor value
// Formula: R = t_rise / (0.8473 * C_bus)
// where t_rise is typically 1000ns for Standard/Fast, 300ns for Fast+, 80ns for HS
float calculate_pullup_resistor(float bus_cap_pf, i2c_speed_mode_t mode) {
    float t_rise_ns;
    
    switch(mode) {
        case I2C_STANDARD_MODE:
        case I2C_FAST_MODE:
            t_rise_ns = 1000.0f;  // 1000ns max rise time
            break;
        case I2C_FAST_MODE_PLUS:
            t_rise_ns = 300.0f;   // 300ns max rise time
            break;
        case I2C_HIGH_SPEED_MODE:
            t_rise_ns = 80.0f;    // 80ns max rise time
            break;
        default:
            t_rise_ns = 1000.0f;
    }
    
    // Convert pF to F: pF * 1e-12
    float bus_cap_f = bus_cap_pf * 1e-12f;
    float t_rise_s = t_rise_ns * 1e-9f;
    
    // Minimum resistor: R_min = t_rise / (0.8473 * C)
    float r_min = t_rise_s / (0.8473f * bus_cap_f);
    
    return r_min;
}

// Calculate maximum bus capacitance for given pull-up resistor
float calculate_max_capacitance(float pullup_ohms, i2c_speed_mode_t mode) {
    float t_rise_ns;
    
    switch(mode) {
        case I2C_STANDARD_MODE:
        case I2C_FAST_MODE:
            t_rise_ns = 1000.0f;
            break;
        case I2C_FAST_MODE_PLUS:
            t_rise_ns = 300.0f;
            break;
        case I2C_HIGH_SPEED_MODE:
            t_rise_ns = 80.0f;
            break;
        default:
            t_rise_ns = 1000.0f;
    }
    
    float t_rise_s = t_rise_ns * 1e-9f;
    
    // C_max = t_rise / (0.8473 * R)
    float c_max_f = t_rise_s / (0.8473f * pullup_ohms);
    
    // Convert to pF
    return c_max_f * 1e12f;
}

// Print detailed capacitance report
void print_capacitance_report(const i2c_bus_components_t* components, 
                               i2c_speed_mode_t mode) {
    const char* mode_names[] = {
        "Standard Mode (100 kHz)",
        "Fast Mode (400 kHz)",
        "Fast Mode Plus (1 MHz)",
        "High-Speed Mode (3.4 MHz)"
    };
    
    float total_cap = calculate_bus_capacitance(components);
    bool is_valid = validate_capacitance(total_cap, mode);
    float min_pullup = calculate_pullup_resistor(total_cap, mode);
    
    printf("\n========== I2C Bus Capacitance Report ==========\n");
    printf("Target Mode: %s\n\n", mode_names[mode]);
    
    printf("Capacitance Components:\n");
    printf("  PCB Trace: %.1f cm × %.1f pF/m = %.2f pF\n",
           components->trace_length_m * 100,
           components->trace_cap_per_m,
           components->trace_length_m * components->trace_cap_per_m);
    printf("  Devices: %d × %.1f pF = %.2f pF\n",
           components->num_devices,
           components->device_cap_pf,
           components->num_devices * components->device_cap_pf);
    printf("  Connectors: %d × %.1f pF = %.2f pF\n",
           components->num_connectors,
           components->connector_cap_pf,
           components->num_connectors * components->connector_cap_pf);
    printf("  Parasitic: %.2f pF\n", components->parasitic_cap_pf);
    printf("  -----------------------------------\n");
    printf("  TOTAL: %.2f pF\n\n", total_cap);
    
    printf("Validation:\n");
    printf("  Maximum allowed: %d pF\n", I2C_CAP_LIMITS[mode]);
    printf("  Status: %s\n", is_valid ? "PASS ✓" : "FAIL ✗");
    printf("  Margin: %.2f pF (%.1f%%)\n\n",
           I2C_CAP_LIMITS[mode] - total_cap,
           ((I2C_CAP_LIMITS[mode] - total_cap) / I2C_CAP_LIMITS[mode]) * 100);
    
    printf("Pull-up Resistor:\n");
    printf("  Minimum required: %.0f Ω (%.2f kΩ)\n",
           min_pullup, min_pullup / 1000.0f);
    printf("  Recommended: %.2f kΩ (standard value)\n",
           ceil(min_pullup / 1000.0f));
    printf("===============================================\n\n");
}

int main() {
    // Example 1: Typical sensor system
    i2c_bus_components_t sensor_bus = {
        .trace_length_m = 0.15f,      // 15 cm trace
        .trace_cap_per_m = 35.0f,     // 35 pF/m typical
        .num_devices = 4,             // 4 sensors
        .device_cap_pf = 6.0f,        // 6 pF per device
        .num_connectors = 1,          // 1 connector
        .connector_cap_pf = 4.0f,     // 4 pF connector
        .parasitic_cap_pf = 8.0f      // 8 pF estimated parasitic
    };
    
    print_capacitance_report(&sensor_bus, I2C_FAST_MODE);
    
    // Example 2: Long distance system
    i2c_bus_components_t long_bus = {
        .trace_length_m = 1.0f,       // 1 meter trace
        .trace_cap_per_m = 40.0f,     // 40 pF/m
        .num_devices = 8,             // 8 devices
        .device_cap_pf = 7.0f,        // 7 pF per device
        .num_connectors = 3,          // 3 connectors
        .connector_cap_pf = 5.0f,     // 5 pF per connector
        .parasitic_cap_pf = 15.0f     // 15 pF estimated parasitic
    };
    
    print_capacitance_report(&long_bus, I2C_STANDARD_MODE);
    
    // Demonstrate maximum capacitance calculation
    printf("Maximum Capacitance for 4.7kΩ Pull-up:\n");
    for (int mode = 0; mode < 4; mode++) {
        float max_cap = calculate_max_capacitance(4700.0f, (i2c_speed_mode_t)mode);
        printf("  %s: %.2f pF\n", 
               mode == 0 ? "Standard" : mode == 1 ? "Fast" : 
               mode == 2 ? "Fast+" : "High-Speed",
               max_cap);
    }
    
    return 0;
}