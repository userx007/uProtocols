// Arduino/ESP32 I2C Pull-up Resistor Calculator and Validator
// This can help diagnose pull-up issues in real-time

#include <Wire.h>

class I2CPullupCalculator {
private:
    float vcc;
    float busCapacitance;
    uint32_t frequency;
    
    // Get max rise time based on frequency
    float getMaxRiseTime() {
        if (frequency <= 100000) return 1000e-9;      // Standard: 1000ns
        if (frequency <= 400000) return 300e-9;       // Fast: 300ns
        if (frequency <= 1000000) return 120e-9;      // Fast+: 120ns
        return 80e-9;                                  // High speed: ~80ns
    }
    
public:
    I2CPullupCalculator(float supplyVoltage, float capacitance_pF, uint32_t freq_Hz) 
        : vcc(supplyVoltage), busCapacitance(capacitance_pF), frequency(freq_Hz) {}
    
    void calculate() {
        Serial.println("\n=== I2C Pull-up Resistor Calculator ===");
        Serial.print("VCC: "); Serial.print(vcc); Serial.println(" V");
        Serial.print("Bus Capacitance: "); Serial.print(busCapacitance); Serial.println(" pF");
        Serial.print("I2C Frequency: "); Serial.print(frequency / 1000.0); Serial.println(" kHz");
        
        // Minimum resistance: (VCC - VOL) / IOL
        float volMax = 0.4;  // V
        float iol = 0.003;   // 3mA
        float rMin = (vcc - volMax) / iol;
        
        // Maximum resistance: tr / (0.8473 × Cb)
        float trSeconds = getMaxRiseTime();
        float cbFarads = busCapacitance * 1e-12;
        float rMax = trSeconds / (0.8473 * cbFarads);
        
        Serial.println("\nResults:");
        Serial.print("  Min Resistance: "); 
        Serial.print(rMin); Serial.print(" Ω (");
        Serial.print(rMin / 1000.0, 2); Serial.println(" kΩ)");
        
        Serial.print("  Max Resistance: ");
        Serial.print(rMax); Serial.print(" Ω (");
        Serial.print(rMax / 1000.0, 2); Serial.println(" kΩ)");
        
        if (rMax > rMin) {
            float recommended = sqrt(rMin * rMax);
            Serial.print("  Recommended: ");
            Serial.print(recommended); Serial.print(" Ω (");
            Serial.print(recommended / 1000.0, 2); Serial.println(" kΩ)");
            
            // Suggest standard values
            Serial.println("\n  Standard resistor suggestions:");
            if (recommended >= 2000 && recommended <= 3000) Serial.println("    → 2.2 kΩ (E12)");
            if (recommended >= 3000 && recommended <= 5000) Serial.println("    → 4.7 kΩ (E12)");
            if (recommended >= 5000 && recommended <= 12000) Serial.println("    → 10 kΩ (E12)");
        } else {
            Serial.println("  ✗ ERROR: No valid range!");
            Serial.println("    Try: Lower speed, reduce capacitance, or use active pull-ups");
        }
    }
    
    // Measure actual rise time (requires oscilloscope or logic analyzer)
    // This demonstrates the concept - actual implementation needs hardware timing
    void estimateRiseTime() {
        Serial.println("\n=== Rise Time Estimation ===");
        Serial.println("Connect oscilloscope to SDA/SCL to measure actual rise time.");
        Serial.println("Expected max rise time: ");
        Serial.print(getMaxRiseTime() * 1e9);
        Serial.println(" ns");
    }
};

// Estimate bus capacitance based on setup
float estimateBusCapacitance(int numDevices, float wireLength_cm) {
    // Rough estimates:
    // - Each device: ~5-10 pF
    // - Wire/trace: ~1-2 pF per cm
    float deviceCap = numDevices * 7.5;  // pF
    float wireCap = wireLength_cm * 1.5; // pF
    float pcbCap = 20.0;                  // pF (parasitic)
    
    return deviceCap + wireCap + pcbCap;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("I2C Pull-up Resistor Calculator");
    Serial.println("================================\n");
    
    // Example 1: Typical Arduino setup
    Serial.println("Example 1: Typical Arduino 3.3V setup");
    float cap1 = estimateBusCapacitance(2, 10.0); // 2 devices, 10cm wire
    I2CPullupCalculator calc1(3.3, cap1, 100000);
    calc1.calculate();
    
    // Example 2: ESP32 Fast Mode
    Serial.println("\n\nExample 2: ESP32 Fast Mode (400 kHz)");
    float cap2 = estimateBusCapacitance(3, 15.0); // 3 devices, 15cm wire
    I2CPullupCalculator calc2(3.3, cap2, 400000);
    calc2.calculate();
    
    // Example 3: 5V Arduino with many devices
    Serial.println("\n\nExample 3: 5V system with multiple devices");
    float cap3 = estimateBusCapacitance(5, 20.0); // 5 devices, 20cm wire
    I2CPullupCalculator calc3(5.0, cap3, 100000);
    calc3.calculate();
    
    // Initialize I2C with calculated frequency
    Wire.begin();
    Wire.setClock(100000); // Use your calculated frequency
    
    Serial.println("\n\nI2C initialized. Scanner starting...\n");
}

void loop() {
    // I2C bus scanner to verify pull-ups are working
    static unsigned long lastScan = 0;
    
    if (millis() - lastScan > 5000) { // Scan every 5 seconds
        lastScan = millis();
        
        Serial.println("Scanning I2C bus...");
        int deviceCount = 0;
        
        for (byte addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            byte error = Wire.endTransmission();
            
            if (error == 0) {
                Serial.print("  Device found at 0x");
                if (addr < 16) Serial.print("0");
                Serial.println(addr, HEX);
                deviceCount++;
            }
        }
        
        if (deviceCount == 0) {
            Serial.println("  No devices found!");
            Serial.println("  Check: Pull-up resistors, wiring, device power");
        } else {
            Serial.print("  Total devices: ");
            Serial.println(deviceCount);
        }
        Serial.println();
    }
}