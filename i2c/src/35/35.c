// i2c_hal.h - Abstract Interface
#ifndef I2C_HAL_H
#define I2C_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Error codes
typedef enum {
    I2C_OK = 0,
    I2C_ERROR_TIMEOUT,
    I2C_ERROR_NACK,
    I2C_ERROR_BUS,
    I2C_ERROR_INVALID_PARAM,
    I2C_ERROR_NOT_INITIALIZED
} i2c_status_t;

// I2C configuration
typedef struct {
    uint32_t clock_speed;  // Clock speed in Hz (e.g., 100000 for 100kHz)
    uint8_t address_mode;  // 7-bit or 10-bit addressing
    uint32_t timeout_ms;   // Timeout in milliseconds
} i2c_config_t;

// Forward declaration of platform-specific handle
typedef struct i2c_handle i2c_handle_t;

// Abstract interface - implemented by each platform
typedef struct {
    i2c_status_t (*init)(i2c_handle_t **handle, const i2c_config_t *config);
    i2c_status_t (*deinit)(i2c_handle_t *handle);
    i2c_status_t (*write)(i2c_handle_t *handle, uint8_t addr, const uint8_t *data, size_t len);
    i2c_status_t (*read)(i2c_handle_t *handle, uint8_t addr, uint8_t *data, size_t len);
    i2c_status_t (*write_read)(i2c_handle_t *handle, uint8_t addr, 
                               const uint8_t *wdata, size_t wlen,
                               uint8_t *rdata, size_t rlen);
    i2c_status_t (*write_reg)(i2c_handle_t *handle, uint8_t addr, uint8_t reg, uint8_t value);
    i2c_status_t (*read_reg)(i2c_handle_t *handle, uint8_t addr, uint8_t reg, uint8_t *value);
    bool (*is_device_ready)(i2c_handle_t *handle, uint8_t addr);
} i2c_driver_t;

// Global driver instance (set by platform-specific code)
extern const i2c_driver_t *g_i2c_driver;

// Convenience wrappers
static inline i2c_status_t i2c_init(i2c_handle_t **handle, const i2c_config_t *config) {
    return g_i2c_driver->init(handle, config);
}

static inline i2c_status_t i2c_deinit(i2c_handle_t *handle) {
    return g_i2c_driver->deinit(handle);
}

static inline i2c_status_t i2c_write(i2c_handle_t *handle, uint8_t addr, 
                                      const uint8_t *data, size_t len) {
    return g_i2c_driver->write(handle, addr, data, len);
}

static inline i2c_status_t i2c_read(i2c_handle_t *handle, uint8_t addr, 
                                     uint8_t *data, size_t len) {
    return g_i2c_driver->read(handle, addr, data, len);
}

static inline i2c_status_t i2c_write_read(i2c_handle_t *handle, uint8_t addr,
                                          const uint8_t *wdata, size_t wlen,
                                          uint8_t *rdata, size_t rlen) {
    return g_i2c_driver->write_read(handle, addr, wdata, wlen, rdata, rlen);
}

static inline i2c_status_t i2c_write_reg(i2c_handle_t *handle, uint8_t addr, 
                                         uint8_t reg, uint8_t value) {
    return g_i2c_driver->write_reg(handle, addr, reg, value);
}

static inline i2c_status_t i2c_read_reg(i2c_handle_t *handle, uint8_t addr, 
                                        uint8_t reg, uint8_t *value) {
    return g_i2c_driver->read_reg(handle, addr, reg, value);
}

static inline bool i2c_is_device_ready(i2c_handle_t *handle, uint8_t addr) {
    return g_i2c_driver->is_device_ready(handle, addr);
}

#endif // I2C_HAL_H

// ============================================================================
// PLATFORM IMPLEMENTATION 1: Linux (using /dev/i2c-X)
// ============================================================================
// i2c_linux.c
#ifdef PLATFORM_LINUX

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct i2c_handle {
    int fd;
    uint32_t timeout_ms;
};

static i2c_status_t linux_init(i2c_handle_t **handle, const i2c_config_t *config) {
    if (!handle || !config) return I2C_ERROR_INVALID_PARAM;
    
    i2c_handle_t *h = malloc(sizeof(i2c_handle_t));
    if (!h) return I2C_ERROR_NOT_INITIALIZED;
    
    // Open I2C bus (typically /dev/i2c-1 on Raspberry Pi)
    h->fd = open("/dev/i2c-1", O_RDWR);
    if (h->fd < 0) {
        free(h);
        return I2C_ERROR_NOT_INITIALIZED;
    }
    
    h->timeout_ms = config->timeout_ms;
    *handle = h;
    return I2C_OK;
}

static i2c_status_t linux_deinit(i2c_handle_t *handle) {
    if (!handle) return I2C_ERROR_INVALID_PARAM;
    close(handle->fd);
    free(handle);
    return I2C_OK;
}

static i2c_status_t linux_write(i2c_handle_t *handle, uint8_t addr, 
                                const uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    if (ioctl(handle->fd, I2C_SLAVE, addr) < 0) {
        return I2C_ERROR_NACK;
    }
    
    if (write(handle->fd, data, len) != (ssize_t)len) {
        return I2C_ERROR_BUS;
    }
    
    return I2C_OK;
}

static i2c_status_t linux_read(i2c_handle_t *handle, uint8_t addr, 
                               uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    if (ioctl(handle->fd, I2C_SLAVE, addr) < 0) {
        return I2C_ERROR_NACK;
    }
    
    if (read(handle->fd, data, len) != (ssize_t)len) {
        return I2C_ERROR_BUS;
    }
    
    return I2C_OK;
}

static i2c_status_t linux_write_read(i2c_handle_t *handle, uint8_t addr,
                                     const uint8_t *wdata, size_t wlen,
                                     uint8_t *rdata, size_t rlen) {
    i2c_status_t status;
    status = linux_write(handle, addr, wdata, wlen);
    if (status != I2C_OK) return status;
    return linux_read(handle, addr, rdata, rlen);
}

static i2c_status_t linux_write_reg(i2c_handle_t *handle, uint8_t addr, 
                                    uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return linux_write(handle, addr, buf, 2);
}

static i2c_status_t linux_read_reg(i2c_handle_t *handle, uint8_t addr, 
                                   uint8_t reg, uint8_t *value) {
    return linux_write_read(handle, addr, &reg, 1, value, 1);
}

static bool linux_is_device_ready(i2c_handle_t *handle, uint8_t addr) {
    if (!handle) return false;
    return ioctl(handle->fd, I2C_SLAVE, addr) >= 0;
}

const i2c_driver_t i2c_linux_driver = {
    .init = linux_init,
    .deinit = linux_deinit,
    .write = linux_write,
    .read = linux_read,
    .write_read = linux_write_read,
    .write_reg = linux_write_reg,
    .read_reg = linux_read_reg,
    .is_device_ready = linux_is_device_ready
};

const i2c_driver_t *g_i2c_driver = &i2c_linux_driver;

#endif // PLATFORM_LINUX

// ============================================================================
// PLATFORM IMPLEMENTATION 2: STM32 HAL
// ============================================================================
// i2c_stm32.c
#ifdef PLATFORM_STM32

#include "stm32f4xx_hal.h"

struct i2c_handle {
    I2C_HandleTypeDef *hi2c;
    uint32_t timeout_ms;
};

static i2c_status_t stm32_init(i2c_handle_t **handle, const i2c_config_t *config) {
    if (!handle || !config) return I2C_ERROR_INVALID_PARAM;
    
    i2c_handle_t *h = malloc(sizeof(i2c_handle_t));
    if (!h) return I2C_ERROR_NOT_INITIALIZED;
    
    // Allocate STM32 HAL handle
    h->hi2c = malloc(sizeof(I2C_HandleTypeDef));
    if (!h->hi2c) {
        free(h);
        return I2C_ERROR_NOT_INITIALIZED;
    }
    
    // Configure I2C peripheral (I2C1 example)
    h->hi2c->Instance = I2C1;
    h->hi2c->Init.ClockSpeed = config->clock_speed;
    h->hi2c->Init.DutyCycle = I2C_DUTYCYCLE_2;
    h->hi2c->Init.OwnAddress1 = 0;
    h->hi2c->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    h->hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h->hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    h->hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    if (HAL_I2C_Init(h->hi2c) != HAL_OK) {
        free(h->hi2c);
        free(h);
        return I2C_ERROR_NOT_INITIALIZED;
    }
    
    h->timeout_ms = config->timeout_ms;
    *handle = h;
    return I2C_OK;
}

static i2c_status_t stm32_deinit(i2c_handle_t *handle) {
    if (!handle) return I2C_ERROR_INVALID_PARAM;
    HAL_I2C_DeInit(handle->hi2c);
    free(handle->hi2c);
    free(handle);
    return I2C_OK;
}

static i2c_status_t stm32_write(i2c_handle_t *handle, uint8_t addr, 
                                const uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(handle->hi2c, addr << 1, 
                                                        (uint8_t*)data, len, 
                                                        handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static i2c_status_t stm32_read(i2c_handle_t *handle, uint8_t addr, 
                               uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(handle->hi2c, addr << 1, 
                                                       data, len, 
                                                       handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static i2c_status_t stm32_write_read(i2c_handle_t *handle, uint8_t addr,
                                     const uint8_t *wdata, size_t wlen,
                                     uint8_t *rdata, size_t rlen) {
    i2c_status_t status;
    status = stm32_write(handle, addr, wdata, wlen);
    if (status != I2C_OK) return status;
    return stm32_read(handle, addr, rdata, rlen);
}

static i2c_status_t stm32_write_reg(i2c_handle_t *handle, uint8_t addr, 
                                    uint8_t reg, uint8_t value) {
    if (!handle) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(handle->hi2c, addr << 1, reg, 
                                                  I2C_MEMADD_SIZE_8BIT, &value, 
                                                  1, handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static i2c_status_t stm32_read_reg(i2c_handle_t *handle, uint8_t addr, 
                                   uint8_t reg, uint8_t *value) {
    if (!handle || !value) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(handle->hi2c, addr << 1, reg, 
                                                 I2C_MEMADD_SIZE_8BIT, value, 
                                                 1, handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static bool stm32_is_device_ready(i2c_handle_t *handle, uint8_t addr) {
    if (!handle) return false;
    return HAL_I2C_IsDeviceReady(handle->hi2c, addr << 1, 3, 
                                  handle->timeout_ms) == HAL_OK;
}

const i2c_driver_t i2c_stm32_driver = {
    .init = stm32_init,
    .deinit = stm32_deinit,
    .write = stm32_write,
    .read = stm32_read,
    .write_read = stm32_write_read,
    .write_reg = stm32_write_reg,
    .read_reg = stm32_read_reg,
    .is_device_ready = stm32_is_device_ready
};

const i2c_driver_t *g_i2c_driver = &i2c_stm32_driver;

#endif // PLATFORM_STM32

// ============================================================================
// APPLICATION CODE - Platform Independent!
// ============================================================================
// sensor_app.c

#include <stdio.h>

#define BME280_ADDR 0x76
#define BME280_CHIP_ID_REG 0xD0

int main(void) {
    i2c_handle_t *i2c;
    
    // Initialize I2C with standard 100kHz speed
    i2c_config_t config = {
        .clock_speed = 100000,
        .address_mode = 7,
        .timeout_ms = 1000
    };
    
    if (i2c_init(&i2c, &config) != I2C_OK) {
        printf("Failed to initialize I2C\n");
        return -1;
    }
    
    // Check if BME280 sensor is present
    if (i2c_is_device_ready(i2c, BME280_ADDR)) {
        printf("BME280 sensor found!\n");
        
        // Read chip ID
        uint8_t chip_id;
        if (i2c_read_reg(i2c, BME280_ADDR, BME280_CHIP_ID_REG, &chip_id) == I2C_OK) {
            printf("Chip ID: 0x%02X\n", chip_id);
        }
        
        // Write to configuration register
        i2c_write_reg(i2c, BME280_ADDR, 0xF4, 0x27);  // Normal mode
    } else {
        printf("BME280 sensor not found\n");
    }
    
    // Cleanup
    i2c_deinit(i2c);
    return 0;
}