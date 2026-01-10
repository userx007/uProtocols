// Calculate pull-up resistor values

#include <stdio.h>
#include <math.h>

// I2C speed modes
typedef enum {
    I2C_STANDARD_MODE = 0,    // 100 kHz
    I2C_FAST_MODE = 1,        // 400 kHz
    I2C_FAST_MODE_PLUS = 2,   // 1 MHz
    I2C_HIGH_SPEED_MODE = 3   // 3.4 MHz
} i2c_speed_mode_t;

// Configuration structure
typedef struct {
    float vcc;                 // Supply voltage (V)
    float vol_max;            // Max LOW-level output voltage (V)
    float iol;                // LOW-level output current (A)
    float bus_capacitance;    // Total bus capacitance (pF)
    i2c_speed_mode_t mode;    // Speed mode
} i2c_config_t;

// Result structure
typedef struct {
    float min_resistance;     // Minimum resistor value (ohms)
    float max_resistance;     // Maximum resistor value (ohms)
    float recommended;        // Recommended value (ohms)
    int is_valid;            // 1 if valid range exists
} resistor_result_t;

// Get maximum rise time for speed mode (nanoseconds)
float get_max_rise_time(i2c_speed_mode_t mode) {
    switch (mode) {
        case I2C_STANDARD_MODE:
            return 1000.0;    // 1000 ns
        case I2C_FAST_MODE:
            return 300.0;     // 300 ns
        case I2C_FAST_MODE_PLUS:
            return 120.0;     // 120 ns
        case I2C_HIGH_SPEED_MODE:
            return 80.0;      // 80 ns (approximate)
        default:
            return 1000.0;
    }
}

// Get speed mode name
const char* get_speed_mode_name(i2c_speed_mode_t mode) {
    switch (mode) {
        case I2C_STANDARD_MODE:
            return "Standard Mode (100 kHz)";
        case I2C_FAST_MODE:
            return "Fast Mode (400 kHz)";
        case I2C_FAST_MODE_PLUS:
            return "Fast Mode Plus (1 MHz)";
        case I2C_HIGH_SPEED_MODE:
            return "High Speed Mode (3.4 MHz)";
        default:
            return "Unknown";
    }
}

// Calculate pull-up resistor values
resistor_result_t calculate_pullup_resistor(const i2c_config_t* config) {
    resistor_result_t result = {0};
    
    // Calculate minimum resistance
    // Rp(min) = (VCC - VOL(max)) / IOL
    result.min_resistance = (config->vcc - config->vol_max) / config->iol;
    
    // Calculate maximum resistance
    // Rp(max) = tr / (0.8473 × Cb)
    // Convert capacitance from pF to F and rise time from ns to s
    float tr_seconds = get_max_rise_time(config->mode) * 1e-9;
    float cb_farads = config->bus_capacitance * 1e-12;
    result.max_resistance = tr_seconds / (0.8473 * cb_farads);
    
    // Check if valid range exists
    result.is_valid = (result.max_resistance > result.min_resistance);
    
    // Calculate recommended value (geometric mean of min and max)
    if (result.is_valid) {
        result.recommended = sqrt(result.min_resistance * result.max_resistance);
        
        // Round to nearest standard resistor value (E12 series)
        float e12_values[] = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2};
        float magnitude = pow(10, floor(log10(result.recommended)));
        float normalized = result.recommended / magnitude;
        
        float closest = e12_values[0];
        float min_diff = fabs(normalized - closest);
        
        for (int i = 1; i < 12; i++) {
            float diff = fabs(normalized - e12_values[i]);
            if (diff < min_diff) {
                min_diff = diff;
                closest = e12_values[i];
            }
        }
        
        result.recommended = closest * magnitude;
    }
    
    return result;
}

// Print results
void print_results(const i2c_config_t* config, const resistor_result_t* result) {
    printf("\n=== I2C Pull-up Resistor Calculation ===\n\n");
    printf("Configuration:\n");
    printf("  Supply Voltage (VCC): %.2f V\n", config->vcc);
    printf("  Bus Capacitance: %.1f pF\n", config->bus_capacitance);
    printf("  Speed Mode: %s\n", get_speed_mode_name(config->mode));
    printf("  Max Rise Time: %.0f ns\n\n", get_max_rise_time(config->mode));
    
    printf("Results:\n");
    printf("  Minimum Resistance: %.0f Ω (%.2f kΩ)\n", 
           result->min_resistance, result->min_resistance / 1000.0);
    printf("  Maximum Resistance: %.0f Ω (%.2f kΩ)\n", 
           result->max_resistance, result->max_resistance / 1000.0);
    
    if (result->is_valid) {
        printf("  Recommended Value: %.0f Ω (%.2f kΩ)\n", 
               result->recommended, result->recommended / 1000.0);
        printf("\n  ✓ Valid resistor range found!\n");
    } else {
        printf("\n  ✗ WARNING: No valid resistor range!\n");
        printf("    Bus capacitance too high or speed too fast.\n");
        printf("    Consider: reducing capacitance, lowering speed, or using active pull-ups.\n");
    }
}

int main(void) {
    // Example 1: Standard mode with typical conditions
    i2c_config_t config1 = {
        .vcc = 3.3,
        .vol_max = 0.4,
        .iol = 0.003,              // 3 mA
        .bus_capacitance = 100.0,  // 100 pF
        .mode = I2C_STANDARD_MODE
    };
    
    resistor_result_t result1 = calculate_pullup_resistor(&config1);
    print_results(&config1, &result1);
    
    // Example 2: Fast mode with higher capacitance
    printf("\n\n");
    i2c_config_t config2 = {
        .vcc = 5.0,
        .vol_max = 0.4,
        .iol = 0.003,
        .bus_capacitance = 300.0,  // 300 pF
        .mode = I2C_FAST_MODE
    };
    
    resistor_result_t result2 = calculate_pullup_resistor(&config2);
    print_results(&config2, &result2);
    
    // Example 3: Problematic case - too much capacitance
    printf("\n\n");
    i2c_config_t config3 = {
        .vcc = 3.3,
        .vol_max = 0.4,
        .iol = 0.003,
        .bus_capacitance = 500.0,  // High capacitance
        .mode = I2C_FAST_MODE_PLUS
    };
    
    resistor_result_t result3 = calculate_pullup_resistor(&config3);
    print_results(&config3, &result3);
    
    return 0;
}