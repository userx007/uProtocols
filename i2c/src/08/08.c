/*
 * I2C Timing Parameters Implementation in C
 * Demonstrates bit-banged I2C with precise timing control
 */

#include <stdint.h>
#include <stdbool.h>

// Timing constants for different I2C modes (in microseconds)
typedef enum {
    I2C_STANDARD_MODE = 0,  // 100 kHz
    I2C_FAST_MODE = 1,      // 400 kHz
    I2C_FAST_PLUS_MODE = 2  // 1 MHz
} i2c_mode_t;

typedef struct {
    float tSU_DAT;    // Data setup time (us)
    float tHD_DAT;    // Data hold time (us)
    float tHD_STA;    // START hold time (us)
    float tSU_STA;    // START setup time (us)
    float tSU_STO;    // STOP setup time (us)
    float tBUF;       // Bus free time (us)
    float tLOW;       // SCL low period (us)
    float tHIGH;      // SCL high period (us)
    float tr_max;     // Max rise time (us)
    float tf_max;     // Max fall time (us)
} i2c_timing_t;

// Timing specifications for each mode
static const i2c_timing_t i2c_timings[] = {
    // Standard Mode (100 kHz)
    {
        .tSU_DAT = 0.250,  // 250 ns
        .tHD_DAT = 0.300,  // 300 ns (0 min, but 300 recommended)
        .tHD_STA = 4.0,    // 4.0 us
        .tSU_STA = 4.7,    // 4.7 us
        .tSU_STO = 4.0,    // 4.0 us
        .tBUF = 4.7,       // 4.7 us
        .tLOW = 4.7,       // 4.7 us
        .tHIGH = 4.0,      // 4.0 us
        .tr_max = 1.0,     // 1000 ns
        .tf_max = 0.3      // 300 ns
    },
    // Fast Mode (400 kHz)
    {
        .tSU_DAT = 0.100,  // 100 ns
        .tHD_DAT = 0.300,  // 300 ns (0 min)
        .tHD_STA = 0.6,    // 0.6 us
        .tSU_STA = 0.6,    // 0.6 us
        .tSU_STO = 0.6,    // 0.6 us
        .tBUF = 1.3,       // 1.3 us
        .tLOW = 1.3,       // 1.3 us
        .tHIGH = 0.6,      // 0.6 us
        .tr_max = 0.3,     // 300 ns
        .tf_max = 0.3      // 300 ns
    },
    // Fast Mode Plus (1 MHz)
    {
        .tSU_DAT = 0.050,  // 50 ns
        .tHD_DAT = 0.000,  // 0 ns min
        .tHD_STA = 0.26,   // 0.26 us
        .tSU_STA = 0.26,   // 0.26 us
        .tSU_STO = 0.26,   // 0.26 us
        .tBUF = 0.5,       // 0.5 us
        .tLOW = 0.5,       // 0.5 us
        .tHIGH = 0.26,     // 0.26 us
        .tr_max = 0.12,    // 120 ns
        .tf_max = 0.12     // 120 ns
    }
};

// Hardware abstraction (implement these for your platform)
extern void gpio_set_scl(bool high);
extern void gpio_set_sda(bool high);
extern bool gpio_read_sda(void);
extern void delay_us(float microseconds);
extern uint64_t get_timestamp_ns(void); // For timing verification

// I2C Master Context
typedef struct {
    i2c_mode_t mode;
    const i2c_timing_t *timing;
    bool started;
} i2c_master_t;

// Initialize I2C master
void i2c_init(i2c_master_t *i2c, i2c_mode_t mode) {
    i2c->mode = mode;
    i2c->timing = &i2c_timings[mode];
    i2c->started = false;
    
    // Initialize both lines high (idle state)
    gpio_set_scl(true);
    gpio_set_sda(true);
}

// Generate START condition
// Timing: SDA falls while SCL is high
void i2c_start(i2c_master_t *i2c) {
    const i2c_timing_t *t = i2c->timing;
    
    if (i2c->started) {
        // Repeated START: ensure SDA is high first
        gpio_set_sda(true);
        delay_us(t->tSU_STA);  // Setup time for repeated START
        gpio_set_scl(true);
        delay_us(t->tSU_STA);
    } else {
        // Initial START: ensure bus free time has elapsed
        delay_us(t->tBUF);
        gpio_set_scl(true);
        gpio_set_sda(true);
        delay_us(t->tSU_STA);  // SCL must be high before SDA falls
    }
    
    // Generate START: SDA goes low while SCL is high
    gpio_set_sda(false);
    delay_us(t->tHD_STA);  // Hold time after START
    gpio_set_scl(false);
    delay_us(t->tHD_DAT);  // Additional hold time
    
    i2c->started = true;
}

// Generate STOP condition
// Timing: SDA rises while SCL is high
void i2c_stop(i2c_master_t *i2c) {
    const i2c_timing_t *t = i2c->timing;
    
    // Ensure SCL is low and SDA is low
    gpio_set_scl(false);
    gpio_set_sda(false);
    delay_us(t->tLOW);
    
    // Bring SCL high
    gpio_set_scl(true);
    delay_us(t->tSU_STO);  // Setup time for STOP
    
    // Generate STOP: SDA goes high while SCL is high
    gpio_set_sda(true);
    delay_us(t->tBUF);  // Bus free time
    
    i2c->started = false;
}

// Write a single bit with proper timing
void i2c_write_bit(i2c_master_t *i2c, bool bit) {
    const i2c_timing_t *t = i2c->timing;
    
    // Set data line while clock is low
    gpio_set_scl(false);
    gpio_set_sda(bit);
    delay_us(t->tSU_DAT);  // Data setup time before clock high
    
    // Clock high period - data is sampled here
    gpio_set_scl(true);
    delay_us(t->tHIGH);
    
    // Clock low period - data can change after this
    gpio_set_scl(false);
    delay_us(t->tHD_DAT);  // Data hold time
}

// Read a single bit with proper timing
bool i2c_read_bit(i2c_master_t *i2c) {
    const i2c_timing_t *t = i2c->timing;
    bool bit;
    
    // Release SDA (slave will drive it)
    gpio_set_scl(false);
    gpio_set_sda(true);  // Release to high-impedance
    delay_us(t->tSU_DAT);
    
    // Clock high - sample data
    gpio_set_scl(true);
    delay_us(t->tHIGH / 2);  // Sample in middle of high period
    bit = gpio_read_sda();
    delay_us(t->tHIGH / 2);
    
    // Clock low
    gpio_set_scl(false);
    delay_us(t->tHD_DAT);
    
    return bit;
}

// Write a byte and return ACK/NACK
bool i2c_write_byte(i2c_master_t *i2c, uint8_t data) {
    // Write 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        i2c_write_bit(i2c, (data >> i) & 0x01);
    }
    
    // Read ACK bit (0 = ACK, 1 = NACK)
    return !i2c_read_bit(i2c);
}

// Read a byte and send ACK/NACK
uint8_t i2c_read_byte(i2c_master_t *i2c, bool send_ack) {
    uint8_t data = 0;
    
    // Read 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        if (i2c_read_bit(i2c)) {
            data |= (1 << i);
        }
    }
    
    // Send ACK (0) or NACK (1)
    i2c_write_bit(i2c, !send_ack);
    
    return data;
}

// Example: Write to I2C device with timing verification
bool i2c_write_register(i2c_master_t *i2c, uint8_t dev_addr, 
                        uint8_t reg_addr, uint8_t value) {
    bool success = true;
    
    // Start condition
    i2c_start(i2c);
    
    // Send device address with write bit (0)
    if (!i2c_write_byte(i2c, (dev_addr << 1) | 0)) {
        success = false;
        goto cleanup;
    }
    
    // Send register address
    if (!i2c_write_byte(i2c, reg_addr)) {
        success = false;
        goto cleanup;
    }
    
    // Send data
    if (!i2c_write_byte(i2c, value)) {
        success = false;
        goto cleanup;
    }
    
cleanup:
    // Stop condition
    i2c_stop(i2c);
    return success;
}

// Calculate required pull-up resistor for desired rise time
// Vdd = supply voltage, Cb = bus capacitance (pF), tr = rise time (ns)
float calculate_pullup_resistor(float vdd, float cb_pf, float tr_ns) {
    // tr = 0.8473 * R * Cb (approximation for 30% to 70%)
    // R = tr / (0.8473 * Cb)
    float cb_farads = cb_pf * 1e-12;
    float tr_seconds = tr_ns * 1e-9;
    float r_ohms = tr_seconds / (0.8473 * cb_farads);
    return r_ohms;
}

// Example usage
void example_timing_usage(void) {
    i2c_master_t i2c;
    
    // Initialize for Fast Mode (400 kHz)
    i2c_init(&i2c, I2C_FAST_MODE);
    
    // Write to device at address 0x50
    uint8_t device_addr = 0x50;
    uint8_t register_addr = 0x10;
    uint8_t data = 0xAA;
    
    bool result = i2c_write_register(&i2c, device_addr, register_addr, data);
    
    // Calculate pull-up resistor for 300ns rise time
    // Assuming 3.3V, 100pF bus capacitance
    float r_pullup = calculate_pullup_resistor(3.3, 100.0, 300.0);
    // Result: ~3.5kΩ (use standard 3.3kΩ or 4.7kΩ)
}