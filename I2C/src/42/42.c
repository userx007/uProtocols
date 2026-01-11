/*
 * I2C Oscilloscope Analysis and Timing Verification
 * 
 * This code demonstrates I2C communication with timing analysis
 * capabilities for oscilloscope verification and debugging.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Platform-specific includes (example for embedded systems)
#ifdef EMBEDDED
#include "gpio.h"
#include "timer.h"
#else
#include <time.h>
#include <unistd.h>
#endif

/* I2C Timing Parameters (in microseconds) */
typedef struct {
    uint32_t scl_frequency;        // Target SCL frequency in Hz
    uint32_t t_hd_sta;            // Hold time START condition (min)
    uint32_t t_su_sta;            // Setup time START condition (min)
    uint32_t t_su_dat;            // Data setup time (min)
    uint32_t t_hd_dat;            // Data hold time (min)
    uint32_t t_su_sto;            // Setup time STOP condition (min)
    uint32_t t_buf;               // Bus free time between transactions (min)
    uint32_t max_rise_time_ns;    // Maximum rise time in nanoseconds
    uint32_t max_fall_time_ns;    // Maximum fall time in nanoseconds
} I2C_TimingParams;

/* Standard Mode (100 kHz) timing parameters */
const I2C_TimingParams I2C_STANDARD_MODE = {
    .scl_frequency = 100000,      // 100 kHz
    .t_hd_sta = 4000,             // 4.0 µs
    .t_su_sta = 4700,             // 4.7 µs
    .t_su_dat = 250,              // 250 ns
    .t_hd_dat = 0,                // 0 ns (but 300ns typical)
    .t_su_sto = 4000,             // 4.0 µs
    .t_buf = 4700,                // 4.7 µs
    .max_rise_time_ns = 1000,     // 1000 ns max
    .max_fall_time_ns = 300       // 300 ns max
};

/* Fast Mode (400 kHz) timing parameters */
const I2C_TimingParams I2C_FAST_MODE = {
    .scl_frequency = 400000,      // 400 kHz
    .t_hd_sta = 600,              // 0.6 µs
    .t_su_sta = 600,              // 0.6 µs
    .t_su_dat = 100,              // 100 ns
    .t_hd_dat = 0,                // 0 ns (but 300ns typical)
    .t_su_sto = 600,              // 0.6 µs
    .t_buf = 1300,                // 1.3 µs
    .max_rise_time_ns = 300,      // 300 ns max
    .max_fall_time_ns = 300       // 300 ns max
};

/* I2C Signal Timing Measurement */
typedef struct {
    uint32_t rise_time_ns;        // Measured rise time
    uint32_t fall_time_ns;        // Measured fall time
    uint32_t scl_high_time_ns;    // SCL high period
    uint32_t scl_low_time_ns;     // SCL low period
    uint32_t start_setup_ns;      // START condition setup time
    uint32_t stop_setup_ns;       // STOP condition setup time
    bool clock_stretching_detected;
    uint32_t stretch_duration_ns;
} I2C_Measurement;

/* I2C Bus State */
typedef struct {
    volatile uint8_t *sda_port;
    volatile uint8_t *scl_port;
    uint8_t sda_pin;
    uint8_t scl_pin;
    I2C_TimingParams timing;
    I2C_Measurement measurement;
    bool debug_mode;
} I2C_Bus;

/* GPIO Helper Functions (platform-specific) */
static inline void sda_high(I2C_Bus *bus) {
    // Set SDA pin as input (pull-up will bring it high)
    // Actual implementation depends on platform
    #ifdef EMBEDDED
    gpio_set_input(bus->sda_pin);
    #endif
}

static inline void sda_low(I2C_Bus *bus) {
    // Set SDA pin as output and drive low
    #ifdef EMBEDDED
    gpio_set_output(bus->sda_pin);
    gpio_write(bus->sda_pin, 0);
    #endif
}

static inline void scl_high(I2C_Bus *bus) {
    #ifdef EMBEDDED
    gpio_set_input(bus->scl_pin);
    #endif
}

static inline void scl_low(I2C_Bus *bus) {
    #ifdef EMBEDDED
    gpio_set_output(bus->scl_pin);
    gpio_write(bus->scl_pin, 0);
    #endif
}

static inline bool sda_read(I2C_Bus *bus) {
    #ifdef EMBEDDED
    return gpio_read(bus->sda_pin);
    #else
    return true; // Simulation
    #endif
}

static inline bool scl_read(I2C_Bus *bus) {
    #ifdef EMBEDDED
    return gpio_read(bus->scl_pin);
    #else
    return true; // Simulation
    #endif
}

/* High-resolution timing functions */
static uint64_t get_time_ns(void) {
    #ifdef EMBEDDED
    return timer_get_ns();
    #else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    #endif
}

static void delay_ns(uint32_t ns) {
    #ifdef EMBEDDED
    timer_delay_ns(ns);
    #else
    struct timespec ts = {0, ns};
    nanosleep(&ts, NULL);
    #endif
}

/* Measure signal rise time */
static uint32_t measure_rise_time(I2C_Bus *bus, bool is_sda) {
    uint64_t start_time, end_time;
    
    // Wait for line to go LOW
    if (is_sda) {
        while (sda_read(bus));
    } else {
        while (scl_read(bus));
    }
    
    // Trigger rise by releasing line
    if (is_sda) {
        sda_high(bus);
    } else {
        scl_high(bus);
    }
    
    // Measure time from 30% to 70% of VDD (simplified: measure full rise)
    start_time = get_time_ns();
    
    if (is_sda) {
        while (!sda_read(bus)) {
            // Timeout check recommended
        }
    } else {
        while (!scl_read(bus)) {
            // Timeout check recommended
        }
    }
    
    end_time = get_time_ns();
    
    return (uint32_t)(end_time - start_time);
}

/* I2C START condition with timing analysis */
static void i2c_start_with_timing(I2C_Bus *bus) {
    uint64_t time_before, time_after;
    
    // Ensure both lines are high initially
    sda_high(bus);
    scl_high(bus);
    delay_ns(bus->timing.t_buf * 1000);
    
    if (bus->debug_mode) {
        printf("[SCOPE] START condition begin\n");
        printf("[SCOPE] SDA and SCL both HIGH\n");
    }
    
    // START condition: SDA transitions HIGH to LOW while SCL is HIGH
    time_before = get_time_ns();
    sda_low(bus);
    time_after = get_time_ns();
    
    if (bus->debug_mode) {
        printf("[SCOPE] SDA goes LOW (START)\n");
        printf("[SCOPE] SDA fall time: %lu ns\n", 
               (unsigned long)(time_after - time_before));
    }
    
    // Hold time for START condition
    delay_ns(bus->timing.t_hd_sta * 1000);
    
    // Pull SCL low to begin data transfer
    scl_low(bus);
    
    if (bus->debug_mode) {
        printf("[SCOPE] SCL goes LOW (ready for data)\n");
    }
}

/* I2C STOP condition with timing analysis */
static void i2c_stop_with_timing(I2C_Bus *bus) {
    uint64_t time_before, time_after;
    
    // Ensure SDA is low
    sda_low(bus);
    delay_ns(bus->timing.t_hd_dat * 1000);
    
    // Release SCL
    scl_high(bus);
    
    // Wait for clock stretching
    uint64_t stretch_start = get_time_ns();
    while (!scl_read(bus)) {
        // Timeout check recommended
    }
    uint64_t stretch_end = get_time_ns();
    
    if (stretch_end - stretch_start > 1000) {
        bus->measurement.clock_stretching_detected = true;
        bus->measurement.stretch_duration_ns = 
            (uint32_t)(stretch_end - stretch_start);
    }
    
    // Setup time for STOP condition
    delay_ns(bus->timing.t_su_sto * 1000);
    
    if (bus->debug_mode) {
        printf("[SCOPE] STOP condition begin\n");
    }
    
    // STOP condition: SDA transitions LOW to HIGH while SCL is HIGH
    time_before = get_time_ns();
    sda_high(bus);
    time_after = get_time_ns();
    
    bus->measurement.rise_time_ns = (uint32_t)(time_after - time_before);
    
    if (bus->debug_mode) {
        printf("[SCOPE] SDA goes HIGH (STOP)\n");
        printf("[SCOPE] SDA rise time: %u ns\n", 
               bus->measurement.rise_time_ns);
    }
    
    // Bus free time
    delay_ns(bus->timing.t_buf * 1000);
}

/* Write one bit with timing analysis */
static void i2c_write_bit_with_timing(I2C_Bus *bus, bool bit) {
    uint64_t scl_low_start, scl_high_start, scl_high_end;
    
    // Set data line
    if (bit) {
        sda_high(bus);
    } else {
        sda_low(bus);
    }
    
    // Data setup time
    delay_ns(bus->timing.t_su_dat);
    
    // Release clock
    scl_low_start = get_time_ns();
    scl_high(bus);
    
    // Wait for clock to go high (check for stretching)
    while (!scl_read(bus)) {
        // Clock stretching
    }
    scl_high_start = get_time_ns();
    
    // Clock high period
    delay_ns((1000000000UL / bus->timing.scl_frequency) / 2);
    
    scl_high_end = get_time_ns();
    bus->measurement.scl_high_time_ns = 
        (uint32_t)(scl_high_end - scl_high_start);
    
    // Pull clock low
    scl_low(bus);
    
    bus->measurement.scl_low_time_ns = 
        (uint32_t)(scl_high_start - scl_low_start);
    
    if (bus->debug_mode) {
        printf("[SCOPE] Bit: %d, SCL high: %u ns, SCL low: %u ns\n",
               bit, bus->measurement.scl_high_time_ns,
               bus->measurement.scl_low_time_ns);
    }
}

/* Read one bit with timing analysis */
static bool i2c_read_bit_with_timing(I2C_Bus *bus) {
    bool bit;
    
    // Release SDA for slave to control
    sda_high(bus);
    delay_ns(bus->timing.t_su_dat);
    
    // Release clock
    scl_high(bus);
    
    // Wait for clock to go high (check for stretching)
    while (!scl_read(bus)) {
        // Clock stretching
    }
    
    // Clock high period
    delay_ns((1000000000UL / bus->timing.scl_frequency) / 2);
    
    // Read the bit
    bit = sda_read(bus);
    
    // Pull clock low
    scl_low(bus);
    
    if (bus->debug_mode) {
        printf("[SCOPE] Read bit: %d\n", bit);
    }
    
    return bit;
}

/* Write byte and return ACK status */
static bool i2c_write_byte_with_timing(I2C_Bus *bus, uint8_t byte) {
    if (bus->debug_mode) {
        printf("[SCOPE] Writing byte: 0x%02X\n", byte);
    }
    
    // Write 8 bits
    for (int i = 7; i >= 0; i--) {
        i2c_write_bit_with_timing(bus, (byte >> i) & 0x01);
    }
    
    // Read ACK bit
    bool ack = !i2c_read_bit_with_timing(bus);
    
    if (bus->debug_mode) {
        printf("[SCOPE] ACK received: %d\n", ack);
    }
    
    return ack;
}

/* Signal quality analysis */
static void analyze_signal_quality(I2C_Bus *bus) {
    printf("\n=== I2C Signal Quality Analysis ===\n");
    
    // Check rise time
    if (bus->measurement.rise_time_ns > bus->timing.max_rise_time_ns) {
        printf("⚠ WARNING: Rise time (%u ns) exceeds maximum (%u ns)\n",
               bus->measurement.rise_time_ns,
               bus->timing.max_rise_time_ns);
        printf("  → Reduce pull-up resistors or bus capacitance\n");
    } else {
        printf("✓ Rise time within specification: %u ns\n",
               bus->measurement.rise_time_ns);
    }
    
    // Check clock frequency
    uint32_t measured_freq = 1000000000UL / 
        (bus->measurement.scl_high_time_ns + bus->measurement.scl_low_time_ns);
    printf("✓ Measured SCL frequency: %u Hz (target: %u Hz)\n",
           measured_freq, bus->timing.scl_frequency);
    
    // Check clock stretching
    if (bus->measurement.clock_stretching_detected) {
        printf("ℹ Clock stretching detected: %u ns\n",
               bus->measurement.stretch_duration_ns);
    }
    
    printf("=====================================\n\n");
}

/* Example usage */
int main(void) {
    I2C_Bus bus = {0};
    
    // Configure for Fast Mode
    bus.timing = I2C_FAST_MODE;
    bus.debug_mode = true;
    
    printf("Starting I2C Oscilloscope Analysis\n");
    printf("Connect oscilloscope to SDA and SCL lines\n");
    printf("Trigger on START condition (SDA falling edge with SCL high)\n\n");
    
    // Perform a test transaction
    i2c_start_with_timing(&bus);
    
    // Write device address (0x50, write mode)
    uint8_t device_addr = (0x50 << 1) | 0;
    bool ack = i2c_write_byte_with_timing(&bus, device_addr);
    
    if (ack) {
        // Write register address
        i2c_write_byte_with_timing(&bus, 0x00);
        
        // Write data
        i2c_write_byte_with_timing(&bus, 0xAA);
    }
    
    i2c_stop_with_timing(&bus);
    
    // Analyze captured measurements
    analyze_signal_quality(&bus);
    
    return 0;
}