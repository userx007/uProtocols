#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// I2C configuration
#define I2C_PORT i2c0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define I2C_FREQ 100000  // 100 kHz

void scan_i2c_bus() {
    printf("\nScanning I2C bus...\n");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    
    int device_count = 0;
    
    for (int addr = 0; addr <= 0x7F; addr++) {
        // Print row header
        if (addr % 16 == 0) {
            printf("%02x: ", addr);
        }
        
        // Skip reserved addresses
        if (addr < 0x03 || addr > 0x77) {
            printf("   ");
        } else {
            // Try to write 0 bytes to the address
            // If device exists, it will ACK
            uint8_t dummy_data = 0;
            int ret = i2c_write_timeout_us(I2C_PORT, addr, &dummy_data, 0, false, 1000);
            
            if (ret >= 0) {
                printf("%02x ", addr);
                device_count++;
            } else {
                printf("-- ");
            }
        }
        
        // New line every 16 addresses
        if ((addr + 1) % 16 == 0) {
            printf("\n");
        }
    }
    
    printf("\nScan complete. Found %d device(s).\n", device_count);
}

void print_device_info(uint8_t addr) {
    printf("\nDevice at 0x%02X:\n", addr);
    
    // Try to read identification if possible
    // This is device-specific; here's a generic approach
    uint8_t reg = 0x00;
    uint8_t data[4];
    
    int ret = i2c_write_blocking(I2C_PORT, addr, &reg, 1, true);
    if (ret > 0) {
        ret = i2c_read_blocking(I2C_PORT, addr, data, 4, false);
        if (ret > 0) {
            printf("  First 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X\n", 
                   data[0], data[1], data[2], data[3]);
        }
    }
}

int main() {
    // Initialize stdio for USB output
    stdio_init_all();
    
    // Wait for USB serial connection
    sleep_ms(2000);
    
    printf("\n=== Raspberry Pi Pico I2C Scanner ===\n");
    
    // Initialize I2C
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    
    printf("I2C initialized on pins SDA=%d, SCL=%d at %d Hz\n", 
           I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ);
    
    while (true) {
        scan_i2c_bus();
        
        printf("\nPress any key to scan again...\n");
        getchar();
    }
    
    return 0;
}

/*
 * CMakeLists.txt additions:
 * 
 * target_link_libraries(i2c_scanner
 *     pico_stdlib
 *     hardware_i2c
 * )
 * 
 * pico_enable_stdio_usb(i2c_scanner 1)
 * pico_enable_stdio_uart(i2c_scanner 0)
 */