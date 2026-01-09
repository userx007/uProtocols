/**
 * I2C Timing Analysis and Debugging Tools - C++
 * Advanced timing verification, measurement, and troubleshooting
 */

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>

namespace i2c {

// Timing measurement result
struct TimingMeasurement {
    uint64_t timestamp_ns;
    bool scl;
    bool sda;
    std::string event;
};

// Timing analysis result
struct TimingAnalysis {
    double t_su_dat_measured;
    double t_hd_dat_measured;
    double t_low_measured;
    double t_high_measured;
    double rise_time_measured;
    double fall_time_measured;
    bool meets_spec;
    std::vector<std::string> violations;
};

// Bus capacitance calculator
class BusCapacitanceCalculator {
public:
    // Calculate total bus capacitance from components
    static double calculate_total_capacitance(
        double wire_cap_pf_per_cm,
        double wire_length_cm,
        const std::vector<double>& device_caps_pf
    ) {
        double total = wire_cap_pf_per_cm * wire_length_cm;
        for (double cap : device_caps_pf) {
            total += cap;
        }
        return total;
    }
    
    // Calculate capacitance from measured rise time
    static double capacitance_from_rise_time(
        double rise_time_ns,
        double pullup_ohms
    ) {
        // tr ≈ 0.8473 × R × C
        // C = tr / (0.8473 × R)
        double tr_seconds = rise_time_ns * 1e-9;
        return tr_seconds / (0.8473 * pullup_ohms) * 1e12; // Return in pF
    }
};

// Pull-up resistor calculator
class PullupCalculator {
public:
    struct ResistorResult {
        double calculated_value;
        double nearest_standard;
        double actual_rise_time_ns;
        bool meets_spec;
    };
    
    // Standard E12 resistor values
    static const std::vector<double> STANDARD_RESISTORS;
    
    // Calculate optimal pull-up resistor
    static ResistorResult calculate(
        double vdd_volts,
        double bus_cap_pf,
        double target_rise_time_ns,
        double max_rise_time_ns,
        double min_current_ma = 3.0  // Minimum sink current
    ) {
        ResistorResult result;
        
        // Calculate from rise time constraint
        double cb_farads = bus_cap_pf * 1e-12;
        double tr_seconds = target_rise_time_ns * 1e-9;
        result.calculated_value = tr_seconds / (0.8473 * cb_farads);
        
        // Check minimum current constraint (VOL = 0.4V typical)
        double r_max_current = (vdd_volts - 0.4) / (min_current_ma * 1e-3);
        if (result.calculated_value > r_max_current) {
            result.calculated_value = r_max_current;
        }
        
        // Find nearest standard value
        result.nearest_standard = find_nearest_standard(result.calculated_value);
        
        // Verify actual rise time with standard value
        result.actual_rise_time_ns = 0.8473 * result.nearest_standard * 
                                     cb_farads * 1e9;
        result.meets_spec = result.actual_rise_time_ns <= max_rise_time_ns;
        
        return result;
    }
    
private:
    static double find_nearest_standard(double target) {
        double best = STANDARD_RESISTORS[0];
        double min_diff = std::abs(target - best);
        
        for (double value : STANDARD_RESISTORS) {
            double diff = std::abs(target - value);
            if (diff < min_diff) {
                min_diff = diff;
                best = value;
            }
        }
        return best;
    }
};

// E12 standard values (1kΩ to 10kΩ range, most common for I2C)
const std::vector<double> PullupCalculator::STANDARD_RESISTORS = {
    1000, 1200, 1500, 1800, 2200, 2700, 3300, 3900, 4700, 5600, 6800, 8200, 10000
};

// Timing analyzer for captured waveforms
class TimingAnalyzer {
public:
    TimingAnalyzer(double spec_t_su_dat, double spec_t_hd_dat,
                   double spec_t_low, double spec_t_high,
                   double spec_tr_max, double spec_tf_max)
        : spec_t_su_dat_(spec_t_su_dat)
        , spec_t_hd_dat_(spec_t_hd_dat)
        , spec_t_low_(spec_t_low)
        , spec_t_high_(spec_t_high)
        , spec_tr_max_(spec_tr_max)
        , spec_tf_max_(spec_tf_max) {}
    
    // Analyze captured timing data
    TimingAnalysis analyze(const std::vector<TimingMeasurement>& data) {
        TimingAnalysis result = {};
        result.meets_spec = true;
        
        // Find setup and hold times
        analyze_setup_hold(data, result);
        
        // Find clock periods
        analyze_clock_periods(data, result);
        
        // Find rise and fall times
        analyze_transitions(data, result);
        
        // Check against specifications
        check_specifications(result);
        
        return result;
    }
    
private:
    void analyze_setup_hold(const std::vector<TimingMeasurement>& data,
                           TimingAnalysis& result) {
        for (size_t i = 1; i < data.size(); i++) {
            const auto& curr = data[i];
            const auto& prev = data[i-1];
            
            // Find SDA stable before SCL rising (setup time)
            if (curr.scl && !prev.scl && curr.sda == prev.sda) {
                // Look back to find last SDA change
                for (int j = i - 1; j >= 0; j--) {
                    if (data[j].sda != curr.sda) {
                        double setup = (prev.timestamp_ns - data[j].timestamp_ns);
                        result.t_su_dat_measured = std::max(
                            result.t_su_dat_measured, setup
                        );
                        break;
                    }
                }
            }
            
            // Find SDA changes after SCL falling (hold time)
            if (!curr.scl && prev.scl) {
                uint64_t scl_fall_time = curr.timestamp_ns;
                // Look forward for SDA change
                for (size_t j = i + 1; j < data.size(); j++) {
                    if (data[j].sda != curr.sda) {
                        double hold = (data[j].timestamp_ns - scl_fall_time);
                        result.t_hd_dat_measured = std::max(
                            result.t_hd_dat_measured, hold
                        );
                        break;
                    }
                }
            }
        }
    }
    
    void analyze_clock_periods(const std::vector<TimingMeasurement>& data,
                              TimingAnalysis& result) {
        uint64_t last_rising = 0;
        uint64_t last_falling = 0;
        
        for (const auto& sample : data) {
            if (sample.scl && last_falling > 0) {
                double t_low = (sample.timestamp_ns - last_falling);
                result.t_low_measured = std::max(result.t_low_measured, t_low);
            }
            
            if (!sample.scl && last_rising > 0) {
                double t_high = (sample.timestamp_ns - last_rising);
                result.t_high_measured = std::max(result.t_high_measured, t_high);
            }
            
            if (sample.scl) last_rising = sample.timestamp_ns;
            else last_falling = sample.timestamp_ns;
        }
    }
    
    void analyze_transitions(const std::vector<TimingMeasurement>& data,
                           TimingAnalysis& result) {
        // Simplified: find maximum transition time
        // In practice, you'd measure from 30% to 70% of Vdd
        for (size_t i = 1; i < data.size(); i++) {
            const auto& curr = data[i];
            const auto& prev = data[i-1];
            
            double transition_time = curr.timestamp_ns - prev.timestamp_ns;
            
            // Rising edge
            if (curr.scl && !prev.scl) {
                result.rise_time_measured = std::max(
                    result.rise_time_measured, transition_time
                );
            }
            
            // Falling edge
            if (!curr.scl && prev.scl) {
                result.fall_time_measured = std::max(
                    result.fall_time_measured, transition_time
                );
            }
        }
    }
    
    void check_specifications(TimingAnalysis& result) {
        if (result.t_su_dat_measured < spec_t_su_dat_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Setup time violation: " + 
                std::to_string(result.t_su_dat_measured) + 
                " ns < " + std::to_string(spec_t_su_dat_) + " ns"
            );
        }
        
        if (result.t_hd_dat_measured < spec_t_hd_dat_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Hold time violation: " + 
                std::to_string(result.t_hd_dat_measured) + 
                " ns < " + std::to_string(spec_t_hd_dat_) + " ns"
            );
        }
        
        if (result.rise_time_measured > spec_tr_max_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Rise time too slow: " + 
                std::to_string(result.rise_time_measured) + 
                " ns > " + std::to_string(spec_tr_max_) + " ns"
            );
        }
        
        if (result.fall_time_measured > spec_tf_max_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Fall time too slow: " + 
                std::to_string(result.fall_time_measured) + 
                " ns > " + std::to_string(spec_tf_max_) + " ns"
            );
        }
    }
    
    double spec_t_su_dat_;
    double spec_t_hd_dat_;
    double spec_t_low_;
    double spec_t_high_;
    double spec_tr_max_;
    double spec_tf_max_;
};

// Troubleshooting helper
class I2cTroubleshooter {
public:
    static void diagnose_timing_issue(const TimingAnalysis& analysis) {
        std::cout << "=== I2C Timing Diagnosis ===" << std::endl;
        std::cout << std::fixed << std::setprecision(1);
        
        std::cout << "\nMeasured Timings:" << std::endl;
        std::cout << "  Setup time:  " << analysis.t_su_dat_measured << " ns" << std::endl;
        std::cout << "  Hold time:   " << analysis.t_hd_dat_measured << " ns" << std::endl;
        std::cout << "  SCL low:     " << analysis.t_low_measured << " ns" << std::endl;
        std::cout << "  SCL high:    " << analysis.t_high_measured << " ns" << std::endl;
        std::cout << "  Rise time:   " << analysis.rise_time_measured << " ns" << std::endl;
        std::cout << "  Fall time:   " << analysis.fall_time_measured << " ns" << std::endl;
        
        if (!analysis.meets_spec) {
            std::cout << "\n⚠️  TIMING VIOLATIONS DETECTED:" << std::endl;
            for (const auto& violation : analysis.violations) {
                std::cout << "  • " << violation << std::endl;
            }
            
            std::cout << "\nRecommendations:" << std::endl;
            suggest_fixes(analysis);
        } else {
            std::cout << "\n✓ All timing parameters within specification" << std::endl;
        }
    }
    
private:
    static void suggest_fixes(const TimingAnalysis& analysis) {
        for (const auto& violation : analysis.violations) {
            if (violation.find("Rise time") != std::string::npos) {
                std::cout << "  → Reduce pull-up resistor value" << std::endl;
                std::cout << "  → Reduce bus capacitance (shorter wires, fewer devices)" << std::endl;
                std::cout << "  → Use stronger pull-ups or active pull-up circuit" << std::endl;
            }
            if (violation.find("Fall time") != std::string::npos) {
                std::cout << "  → Check device output drivers" << std::endl;
                std::cout << "  → Reduce bus capacitance" << std::endl;
            }
            if (violation.find("Setup time") != std::string::npos) {
                std::cout << "  → Reduce clock frequency" << std::endl;
                std::cout << "  → Check for signal integrity issues" << std::endl;
                std::cout << "  → Verify device timing specifications" << std::endl;
            }
            if (violation.find("Hold time") != std::string::npos) {
                std::cout << "  → Add delay before data transitions" << std::endl;
                std::cout << "  → Check for clock/data skew" << std::endl;
            }
        }
    }
};

} // namespace i2c

// Example usage and test
int main() {
    using namespace i2c;
    
    std::cout << "=== I2C Timing Analysis Example ===" << std::endl;
    
    // Example 1: Calculate pull-up resistor for Fast Mode
    std::cout << "\n1. Pull-up Resistor Calculation (Fast Mode, 400 kHz):" << std::endl;
    double vdd = 3.3;
    double bus_cap = 120.0; // pF
    double target_rise = 250.0; // ns
    double max_rise = 300.0; // ns (Fast mode spec)
    
    auto resistor = PullupCalculator::calculate(
        vdd, bus_cap, target_rise, max_rise
    );
    
    std::cout << "  Bus capacitance: " << bus_cap << " pF" << std::endl;
    std::cout << "  Target rise time: " << target_rise << " ns" << std::endl;
    std::cout << "  Calculated R: " << std::fixed << std::setprecision(0) 
              << resistor.calculated_value << " Ω" << std::endl;
    std::cout << "  Nearest standard: " << resistor.nearest_standard << " Ω" << std::endl;
    std::cout << "  Actual rise time: " << std::setprecision(1) 
              << resistor.actual_rise_time_ns << " ns" << std::endl;
    std::cout << "  Meets spec: " << (resistor.meets_spec ? "✓ Yes" : "✗ No") << std::endl;
    
    // Example 2: Calculate bus capacitance
    std::cout << "\n2. Bus Capacitance Calculation:" << std::endl;
    double wire_cap = 1.2; // pF/cm (typical for PCB trace)
    double wire_length = 10.0; // cm
    std::vector<double> device_caps = {10.0, 15.0, 8.0}; // pF for 3 devices
    
    double total_cap = BusCapacitanceCalculator::calculate_total_capacitance(
        wire_cap, wire_length, device_caps
    );
    
    std::cout << "  Wire: " << wire_length << " cm × " << wire_cap 
              << " pF/cm = " << (wire_cap * wire_length) << " pF" << std::endl;
    std::cout << "  Devices: ";
    for (size_t i = 0; i < device_caps.size(); i++) {
        std::cout << device_caps[i] << " pF";
        if (i < device_caps.size() - 1) std::cout << " + ";
    }
    std::cout << std::endl;
    std::cout << "  Total capacitance: " << total_cap << " pF" << std::endl;
    
    // Example 3: Reverse calculate capacitance from measured rise time
    std::cout << "\n3. Capacitance from Measured Rise Time:" << std::endl;
    double measured_rise = 280.0; // ns
    double pullup = 3300.0; // Ω
    double calc_cap = BusCapacitanceCalculator::capacitance_from_rise_time(
        measured_rise, pullup
    );
    std::cout << "  Measured rise time: " << measured_rise << " ns" << std::endl;
    std::cout << "  Pull-up resistor: " << pullup << " Ω" << std::endl;
    std::cout << "  Calculated capacitance: " << std::setprecision(1) 
              << calc_cap << " pF" << std::endl;
    
    return 0;
}