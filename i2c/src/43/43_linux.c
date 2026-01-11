#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <string.h>

#define I2C_BUS "/dev/i2c-1"  // Change based on your system
#define FIRST_ADDR 0x03       // First valid address
#define LAST_ADDR 0x77        // Last valid address

int main() {
    int file;
    int addr;
    int found_count = 0;
    
    // Open I2C bus
    file = open(I2C_BUS, O_RDWR);
    if (file < 0) {
        perror("Failed to open I2C bus");
        return 1;
    }
    
    printf("Scanning I2C bus %s...\n\n", I2C_BUS);
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    printf("00:          ");
    
    for (addr = 0; addr <= 0x7F; addr++) {
        // Print row header
        if (addr % 16 == 0 && addr != 0) {
            printf("\n%02x: ", addr);
        }
        
        // Skip reserved addresses
        if (addr < FIRST_ADDR || addr > LAST_ADDR) {
            printf("   ");
            continue;
        }
        
        // Try to communicate with device at this address
        if (ioctl(file, I2C_SLAVE, addr) < 0) {
            printf("-- ");
            continue;
        }
        
        // Attempt a read operation to check for ACK
        // Some devices may not like this, but it's the standard approach
        uint8_t buffer;
        int result = read(file, &buffer, 1);
        
        if (result >= 0 || errno == ENXIO) {
            // ENXIO means the address was NACKed (no device)
            if (errno == ENXIO) {
                printf("-- ");
            } else {
                // Device responded!
                printf("%02x ", addr);
                found_count++;
            }
        } else {
            printf("-- ");
        }
        
        errno = 0;  // Reset errno for next iteration
    }
    
    printf("\n\n");
    printf("Scan complete. Found %d device(s).\n", found_count);
    
    close(file);
    return 0;
}

/*
 * Compile: gcc -o i2c_scanner i2c_scanner.c
 * Run: sudo ./i2c_scanner
 * 
 * Note: Requires root/sudo permissions to access I2C devices
 */