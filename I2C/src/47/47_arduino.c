/**
 * I2C Low Power Example for ESP32/Arduino
 * Demonstrates practical power-saving techniques for battery operation
 */

#include <Wire.h>

// ESP32 specific includes for deep sleep
#ifdef ESP32
  #include "esp_sleep.h"
  #include "driver/rtc_io.h"
#endif

// Sensor configuration
#define SENSOR_ADDR 0x48
#define TEMP_REG 0x00

// I2C pins (ESP32)
#define SDA_PIN 21
#define SCL_PIN 22

// Power control pin for pull-up resistors (optional)
#define PULLUP_POWER_PIN 15

// Configuration
#define SLEEP_INTERVAL_SEC 60      // Sleep 60 seconds between readings
#define I2C_CLOCK_SPEED 100000     // 100kHz for lower power
#define USE_FAST_I2C false         // Use standard mode

// Power statistics
struct PowerStats {
  uint32_t wake_count;
  uint32_t successful_reads;
  uint32_t failed_reads;
  float last_battery_voltage;
};

PowerStats stats = {0};

/**
 * Initialize I2C with low-power settings
 */
void initI2C() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_SPEED);
  
  Serial.println("I2C initialized in low-power mode");
  Serial.printf("Clock speed: %d Hz\n", I2C_CLOCK_SPEED);
}

/**
 * Deinitialize I2C to save power
 * Sets pins to input mode with pull-ups disabled
 */
void deinitI2C() {
  Wire.end();
  
  // Configure pins for lowest power consumption
  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
  
  #ifdef ESP32
    // On ESP32, can set to RTC GPIO mode for even lower power
    rtc_gpio_init((gpio_num_t)SDA_PIN);
    rtc_gpio_init((gpio_num_t)SCL_PIN);
    rtc_gpio_set_direction((gpio_num_t)SDA_PIN, RTC_GPIO_MODE_DISABLED);
    rtc_gpio_set_direction((gpio_num_t)SCL_PIN, RTC_GPIO_MODE_DISABLED);
  #endif
}

/**
 * Control external pull-up resistor power (optional)
 * Requires MOSFET circuit to switch pull-up power
 */
void enablePullups() {
  digitalWrite(PULLUP_POWER_PIN, HIGH);
  delay(1); // Stabilization time
}

void disablePullups() {
  digitalWrite(PULLUP_POWER_PIN, LOW);
}

/**
 * Burst read function - reads multiple bytes in one transaction
 */
bool i2cBurstRead(uint8_t devAddr, uint8_t regAddr, uint8_t* data, uint8_t len) {
  Wire.beginTransmission(devAddr);
  Wire.write(regAddr);
  
  if (Wire.endTransmission(false) != 0) { // Repeated start
    return false;
  }
  
  Wire.requestFrom(devAddr, len);
  
  uint8_t i = 0;
  while (Wire.available() && i < len) {
    data[i++] = Wire.read();
  }
  
  return (i == len);
}

/**
 * Read temperature sensor with full power management
 */
bool readTemperatureOptimized(float* temperature) {
  uint8_t data[2];
  bool success = false;
  
  // 1. Enable pull-ups if controlled externally
  #ifdef PULLUP_POWER_PIN
    enablePullups();
  #endif
  
  // 2. Initialize I2C
  initI2C();
  
  // 3. Read sensor data (2 bytes burst read)
  if (i2cBurstRead(SENSOR_ADDR, TEMP_REG, data, 2)) {
    // 4. Convert to temperature
    int16_t raw = (data[0] << 8) | data[1];
    *temperature = raw * 0.0625; // Example conversion
    success = true;
    stats.successful_reads++;
  } else {
    stats.failed_reads++;
  }
  
  // 5. Deinitialize I2C
  deinitI2C();
  
  // 6. Disable pull-ups if controlled externally
  #ifdef PULLUP_POWER_PIN
    disablePullups();
  #endif
  
  return success;
}

/**
 * Configure sensor for low-power operation
 */
bool configureSensorLowPower() {
  initI2C();
  
  Wire.beginTransmission(SENSOR_ADDR);
  Wire.write(0x01); // Config register
  Wire.write(0x60); // Low power mode: shutdown between conversions
  Wire.write(0xA0); // Config: 8 samples average, low power
  
  bool success = (Wire.endTransmission() == 0);
  
  deinitI2C();
  return success;
}

/**
 * Read battery voltage (ESP32 ADC)
 */
float readBatteryVoltage() {
  #ifdef ESP32
    // Assuming voltage divider on ADC pin
    int raw = analogRead(36); // GPIO36 (VP)
    // Convert to voltage (depends on divider ratio)
    return (raw / 4095.0) * 3.3 * 2.0; // Example for 1:1 divider
  #else
    return 3.3; // Dummy value for non-ESP32
  #endif
}

/**
 * Enter deep sleep mode
 */
void enterDeepSleep(uint32_t seconds) {
  Serial.printf("Entering deep sleep for %d seconds\n", seconds);
  Serial.flush(); // Ensure all serial data is sent
  
  #ifdef ESP32
    // Configure wake-up
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    
    // Enter deep sleep
    esp_deep_sleep_start();
  #else
    // For non-ESP32, just use delay (not true low power)
    delay(seconds * 1000);
  #endif
}

/**
 * Calculate power consumption estimate
 */
void printPowerEstimate() {
  // Pull-up resistor power (assuming 10kΩ, 3.3V)
  float pullup_power_mw = (3.3 * 3.3) / 10.0; // ~1.09mW per resistor
  float total_pullup_mw = pullup_power_mw * 2; // SDA and SCL
  
  // Active time per reading (~10ms for I2C transaction)
  float active_time_sec = 0.010;
  
  // Sleep current (ESP32 deep sleep: ~10µA)
  float sleep_current_ua = 10.0;
  
  // Active current (ESP32 + sensor: ~50mA)
  float active_current_ma = 50.0;
  
  // Average current calculation
  float duty_cycle = active_time_sec / SLEEP_INTERVAL_SEC;
  float avg_current_ua = (active_current_ma * 1000 * duty_cycle) + sleep_current_ua;
  
  Serial.println("\n=== Power Estimate ===");
  Serial.printf("Pull-up power: %.2f mW (when active)\n", total_pullup_mw);
  Serial.printf("Active current: %.2f mA\n", active_current_ma);
  Serial.printf("Sleep current: %.2f µA\n", sleep_current_ua);
  Serial.printf("Duty cycle: %.4f%%\n", duty_cycle * 100);
  Serial.printf("Average current: %.2f µA\n", avg_current_ua);
  Serial.printf("\nBattery life (2000mAh):\n");
  Serial.printf("  Estimated: %.1f days\n", (2000.0 * 1000.0) / (avg_current_ua * 24.0));
}

/**
 * Optimized reading cycle
 */
void performReadingCycle() {
  float temperature;
  
  Serial.println("\n--- Wake Up ---");
  stats.wake_count++;
  
  // Read battery voltage
  stats.last_battery_voltage = readBatteryVoltage();
  Serial.printf("Battery: %.2fV\n", stats.last_battery_voltage);
  
  // Read sensor
  if (readTemperatureOptimized(&temperature)) {
    Serial.printf("Temperature: %.2f°C\n", temperature);
    
    // Process data (e.g., send via LoRa, store in flash, etc.)
    // For demonstration, just print
  } else {
    Serial.println("Failed to read sensor!");
  }
  
  // Print statistics
  Serial.printf("Stats: Wake=%d, Success=%d, Failed=%d\n",
                stats.wake_count, stats.successful_reads, stats.failed_reads);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== I2C Low Power Demo ===");
  
  #ifdef ESP32
    // Print wake-up reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("Wake from timer");
    } else {
      Serial.println("Initial boot");
    }
  #endif
  
  // Configure pull-up power control if used
  #ifdef PULLUP_POWER_PIN
    pinMode(PULLUP_POWER_PIN, OUTPUT);
    disablePullups();
  #endif
  
  // Configure sensor on first boot
  if (stats.wake_count == 0) {
    Serial.println("Configuring sensor...");
    if (configureSensorLowPower()) {
      Serial.println("Sensor configured for low power");
    } else {
      Serial.println("Sensor configuration failed!");
    }
    
    printPowerEstimate();
  }
  
  // Perform reading
  performReadingCycle();
  
  // Enter sleep
  enterDeepSleep(SLEEP_INTERVAL_SEC);
}

void loop() {
  // Never reached in deep sleep mode
  // For non-ESP32 or light sleep:
  delay(SLEEP_INTERVAL_SEC * 1000);
  performReadingCycle();
}

/**
 * Alternative: Light sleep (maintains WiFi connection)
 */
#ifdef ESP32
void enterLightSleep(uint32_t milliseconds) {
  // Light sleep maintains RAM and WiFi calibration data
  esp_sleep_enable_timer_wakeup(milliseconds * 1000ULL);
  esp_light_sleep_start();
}
#endif

/**
 * Advanced: Adaptive sleep based on battery level
 */
uint32_t getAdaptiveSleepInterval() {
  float battery = readBatteryVoltage();
  
  if (battery > 3.7) {
    return 60;   // 60 seconds - normal operation
  } else if (battery > 3.5) {
    return 120;  // 2 minutes - extend battery
  } else if (battery > 3.3) {
    return 300;  // 5 minutes - critical battery
  } else {
    return 600;  // 10 minutes - near empty
  }
}