#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial port to connect (Leonardo/Micro)
  
  Wire.begin();
  
  Serial.println("\nI2C Bus Scanner");
  Serial.println("===============");
}

void loop() {
  byte error, address;
  int deviceCount = 0;
  
  Serial.println("\nScanning I2C bus...");
  Serial.println("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
  
  for (address = 0; address <= 0x7F; address++) {
    // Print row header
    if (address % 16 == 0) {
      Serial.print(address < 0x10 ? "0" : "");
      Serial.print(address, HEX);
      Serial.print(": ");
    }
    
    // Skip reserved addresses
    if (address < 0x03 || address > 0x77) {
      Serial.print("   ");
    } else {
      // Attempt to communicate with device
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      
      if (error == 0) {
        // Device found
        Serial.print(address < 0x10 ? " 0" : " ");
        Serial.print(address, HEX);
        deviceCount++;
      } else if (error == 4) {
        // Unknown error
        Serial.print(" ??");
      } else {
        // No device at this address
        Serial.print(" --");
      }
    }
    
    // New line every 16 addresses
    if ((address + 1) % 16 == 0) {
      Serial.println();
    }
  }
  
  Serial.println();
  Serial.print("Scan complete. Found ");
  Serial.print(deviceCount);
  Serial.println(" device(s).");
  
  if (deviceCount > 0) {
    Serial.println("\nCommon I2C device addresses:");
    Serial.println("  0x27, 0x3C, 0x3D: OLED displays");
    Serial.println("  0x48-0x4F: ADS1115, TMP102");
    Serial.println("  0x50-0x57: EEPROM");
    Serial.println("  0x68: MPU6050, DS1307 RTC");
    Serial.println("  0x76, 0x77: BMP280, BME280");
  }
  
  // Wait 5 seconds before next scan
  delay(5000);
}

/*
 * Upload to Arduino board
 * Open Serial Monitor at 115200 baud
 * 
 * Wire.endTransmission() error codes:
 *   0: Success
 *   1: Data too long for transmit buffer
 *   2: NACK on address transmission
 *   3: NACK on data transmission
 *   4: Other error
 */