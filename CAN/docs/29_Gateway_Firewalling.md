# CAN Gateway Firewalling: Detailed Technical Description

## Overview

CAN Gateway Firewalling is a critical security mechanism that implements filtering rules at CAN gateway nodes to control message flow between different CAN network segments. This approach creates security boundaries within vehicular networks, preventing unauthorized access and containing potential security breaches.

## Technical Architecture

### Gateway Positioning

CAN gateways typically sit at strategic points in automotive architectures:

- **Between network domains**: Separating powertrain, chassis, body, and infotainment networks
- **At entry points**: Controlling access from external interfaces (OBD-II, telematics)
- **Between security zones**: Isolating safety-critical systems from less-trusted domains

### Filtering Mechanisms

1. **Whitelist-based filtering**: Only explicitly permitted CAN IDs can traverse
2. **Blacklist-based filtering**: Specific CAN IDs are blocked
3. **Directional control**: Messages allowed only in specified directions
4. **Rate limiting**: Restricting message frequency to prevent flooding
5. **Content inspection**: Validating data field patterns

## C/C++ Implementation Example

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CAN frame structure
typedef struct {
    uint32_t can_id;
    uint8_t can_dlc;
    uint8_t data[8];
} can_frame_t;

// Gateway rule structure
typedef struct {
    uint32_t can_id;
    bool bidirectional;
    bool network_a_to_b;
    bool network_b_to_a;
    uint32_t max_rate_ms;  // Minimum interval between messages
    uint64_t last_forward_time;
} gateway_rule_t;

// Gateway firewall context
typedef struct {
    gateway_rule_t *rules;
    size_t rule_count;
    bool default_allow;  // Default policy
} gateway_firewall_t;

// Initialize firewall with rules
gateway_firewall_t* gateway_init(gateway_rule_t *rules, size_t count, bool default_allow) {
    gateway_firewall_t *gw = malloc(sizeof(gateway_firewall_t));
    gw->rules = malloc(sizeof(gateway_rule_t) * count);
    memcpy(gw->rules, rules, sizeof(gateway_rule_t) * count);
    gw->rule_count = count;
    gw->default_allow = default_allow;
    return gw;
}

// Get current time in milliseconds (platform-specific implementation needed)
uint64_t get_time_ms(void) {
    // Placeholder - implement with actual system time
    return 0;
}

// Check if frame should be forwarded
bool gateway_filter(gateway_firewall_t *gw, can_frame_t *frame, 
                   bool from_network_a, uint64_t current_time) {
    
    // Search for matching rule
    for (size_t i = 0; i < gw->rule_count; i++) {
        gateway_rule_t *rule = &gw->rules[i];
        
        if (rule->can_id == frame->can_id) {
            // Check direction
            bool direction_allowed = false;
            if (from_network_a && rule->network_a_to_b) {
                direction_allowed = true;
            } else if (!from_network_a && rule->network_b_to_a) {
                direction_allowed = true;
            } else if (rule->bidirectional) {
                direction_allowed = true;
            }
            
            if (!direction_allowed) {
                printf("BLOCKED: CAN ID 0x%03X - Direction not allowed\n", frame->can_id);
                return false;
            }
            
            // Check rate limiting
            if (rule->max_rate_ms > 0) {
                uint64_t elapsed = current_time - rule->last_forward_time;
                if (elapsed < rule->max_rate_ms) {
                    printf("BLOCKED: CAN ID 0x%03X - Rate limit exceeded\n", frame->can_id);
                    return false;
                }
                rule->last_forward_time = current_time;
            }
            
            printf("ALLOWED: CAN ID 0x%03X forwarded\n", frame->can_id);
            return true;
        }
    }
    
    // No matching rule - use default policy
    if (gw->default_allow) {
        printf("ALLOWED: CAN ID 0x%03X (default policy)\n", frame->can_id);
        return true;
    } else {
        printf("BLOCKED: CAN ID 0x%03X (default deny)\n", frame->can_id);
        return false;
    }
}

// Example usage
int main(void) {
    // Define gateway rules
    gateway_rule_t rules[] = {
        // Engine control messages - powertrain to chassis only
        {.can_id = 0x100, .network_a_to_b = true, .network_b_to_a = false, 
         .max_rate_ms = 10, .last_forward_time = 0},
        
        // Brake status - bidirectional, no rate limit
        {.can_id = 0x200, .bidirectional = true, .max_rate_ms = 0, 
         .last_forward_time = 0},
        
        // Diagnostic commands - chassis to powertrain only, rate limited
        {.can_id = 0x7DF, .network_a_to_b = false, .network_b_to_a = true,
         .max_rate_ms = 100, .last_forward_time = 0},
    };
    
    // Initialize firewall with default-deny policy
    gateway_firewall_t *gw = gateway_init(rules, 3, false);
    
    // Test frames
    can_frame_t frame1 = {.can_id = 0x100, .can_dlc = 8};
    can_frame_t frame2 = {.can_id = 0x200, .can_dlc = 8};
    can_frame_t frame3 = {.can_id = 0x7DF, .can_dlc = 8};
    can_frame_t frame4 = {.can_id = 0x500, .can_dlc = 8}; // Not in rules
    
    uint64_t time = get_time_ms();
    
    // Test filtering
    gateway_filter(gw, &frame1, true, time);   // Should allow
    gateway_filter(gw, &frame1, false, time);  // Should block (wrong direction)
    gateway_filter(gw, &frame2, true, time);   // Should allow
    gateway_filter(gw, &frame2, false, time);  // Should allow (bidirectional)
    gateway_filter(gw, &frame4, true, time);   // Should block (default deny)
    
    free(gw->rules);
    free(gw);
    return 0;
}
```

## Rust Implementation Example

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};

// CAN frame structure
#[derive(Debug, Clone)]
struct CanFrame {
    can_id: u32,
    data: Vec<u8>,
}

// Direction enum for clarity
#[derive(Debug, Clone, Copy)]
enum Direction {
    AtoB,
    BtoA,
    Bidirectional,
}

// Gateway filtering rule
#[derive(Debug, Clone)]
struct GatewayRule {
    can_id: u32,
    direction: Direction,
    max_rate: Option<Duration>,
    last_forward: Option<Instant>,
}

impl GatewayRule {
    fn new(can_id: u32, direction: Direction, max_rate: Option<Duration>) -> Self {
        GatewayRule {
            can_id,
            direction,
            max_rate,
            last_forward: None,
        }
    }
    
    fn check_direction(&self, from_network_a: bool) -> bool {
        match self.direction {
            Direction::Bidirectional => true,
            Direction::AtoB => from_network_a,
            Direction::BtoA => !from_network_a,
        }
    }
    
    fn check_rate_limit(&mut self, now: Instant) -> bool {
        if let Some(max_rate) = self.max_rate {
            if let Some(last) = self.last_forward {
                let elapsed = now.duration_since(last);
                if elapsed < max_rate {
                    return false; // Rate limit exceeded
                }
            }
            self.last_forward = Some(now);
        }
        true
    }
}

// Gateway firewall
struct GatewayFirewall {
    rules: HashMap<u32, GatewayRule>,
    default_allow: bool,
    stats: FirewallStats,
}

#[derive(Debug, Default)]
struct FirewallStats {
    allowed: u64,
    blocked_direction: u64,
    blocked_rate: u64,
    blocked_default: u64,
}

impl GatewayFirewall {
    fn new(default_allow: bool) -> Self {
        GatewayFirewall {
            rules: HashMap::new(),
            default_allow,
            stats: FirewallStats::default(),
        }
    }
    
    fn add_rule(&mut self, rule: GatewayRule) {
        self.rules.insert(rule.can_id, rule);
    }
    
    fn filter(&mut self, frame: &CanFrame, from_network_a: bool) -> bool {
        let now = Instant::now();
        
        if let Some(rule) = self.rules.get_mut(&frame.can_id) {
            // Check direction
            if !rule.check_direction(from_network_a) {
                self.stats.blocked_direction += 1;
                println!("BLOCKED: CAN ID 0x{:03X} - Direction not allowed", frame.can_id);
                return false;
            }
            
            // Check rate limiting
            if !rule.check_rate_limit(now) {
                self.stats.blocked_rate += 1;
                println!("BLOCKED: CAN ID 0x{:03X} - Rate limit exceeded", frame.can_id);
                return false;
            }
            
            self.stats.allowed += 1;
            println!("ALLOWED: CAN ID 0x{:03X} forwarded", frame.can_id);
            true
        } else {
            // No matching rule - apply default policy
            if self.default_allow {
                self.stats.allowed += 1;
                println!("ALLOWED: CAN ID 0x{:03X} (default policy)", frame.can_id);
                true
            } else {
                self.stats.blocked_default += 1;
                println!("BLOCKED: CAN ID 0x{:03X} (default deny)", frame.can_id);
                false
            }
        }
    }
    
    fn get_stats(&self) -> &FirewallStats {
        &self.stats
    }
}

fn main() {
    // Create firewall with default-deny policy
    let mut firewall = GatewayFirewall::new(false);
    
    // Add rules
    firewall.add_rule(GatewayRule::new(
        0x100,
        Direction::AtoB,
        Some(Duration::from_millis(10)),
    ));
    
    firewall.add_rule(GatewayRule::new(
        0x200,
        Direction::Bidirectional,
        None,
    ));
    
    firewall.add_rule(GatewayRule::new(
        0x7DF,
        Direction::BtoA,
        Some(Duration::from_millis(100)),
    ));
    
    // Test frames
    let test_cases = vec![
        (CanFrame { can_id: 0x100, data: vec![0; 8] }, true, "Engine control A->B"),
        (CanFrame { can_id: 0x100, data: vec![0; 8] }, false, "Engine control B->A"),
        (CanFrame { can_id: 0x200, data: vec![0; 8] }, true, "Brake status A->B"),
        (CanFrame { can_id: 0x200, data: vec![0; 8] }, false, "Brake status B->A"),
        (CanFrame { can_id: 0x500, data: vec![0; 8] }, true, "Unknown ID"),
    ];
    
    println!("=== Gateway Firewall Test ===\n");
    for (frame, from_a, description) in test_cases {
        println!("Testing: {}", description);
        firewall.filter(&frame, from_a);
        println!();
    }
    
    println!("=== Firewall Statistics ===");
    println!("{:#?}", firewall.get_stats());
}
```

## Advanced Features

### Content-Based Filtering

```c
// Extended rule with data validation
typedef struct {
    uint32_t can_id;
    uint8_t data_mask[8];      // Mask for relevant bytes
    uint8_t expected_pattern[8]; // Expected values
    bool validate_content;
} content_rule_t;

bool validate_frame_content(can_frame_t *frame, content_rule_t *rule) {
    if (!rule->validate_content) return true;
    
    for (int i = 0; i < frame->can_dlc; i++) {
        uint8_t masked_data = frame->data[i] & rule->data_mask[i];
        uint8_t expected = rule->expected_pattern[i] & rule->data_mask[i];
        if (masked_data != expected) {
            return false;
        }
    }
    return true;
}
```

### Dynamic Rule Updates

```rust
impl GatewayFirewall {
    fn update_rule(&mut self, can_id: u32, new_direction: Direction) -> Result<(), String> {
        if let Some(rule) = self.rules.get_mut(&can_id) {
            rule.direction = new_direction;
            println!("Updated rule for CAN ID 0x{:03X}", can_id);
            Ok(())
        } else {
            Err(format!("No rule found for CAN ID 0x{:03X}", can_id))
        }
    }
    
    fn remove_rule(&mut self, can_id: u32) -> bool {
        self.rules.remove(&can_id).is_some()
    }
}
```

## Summary

**CAN Gateway Firewalling** provides essential network segmentation and access control in automotive systems by implementing filtering rules at gateway nodes between CAN network segments.

**Key Benefits:**
- **Network Isolation**: Separates critical domains (powertrain, safety) from less-trusted networks (infotainment)
- **Attack Surface Reduction**: Limits lateral movement of attackers across network boundaries
- **Rate Limiting**: Prevents flooding attacks and abnormal traffic patterns
- **Directional Control**: Enforces one-way communication where appropriate for security

**Implementation Considerations:**
- **Whitelist approach** (default-deny) is more secure than blacklisting
- **Performance impact** must be minimal to avoid affecting real-time CAN communication
- Rules should be **stored securely** and protected from unauthorized modification
- **Logging and monitoring** of blocked messages aids security analysis
- **Rule updates** should be authenticated and may require secure boot integration

**Security Limitations:**
- Cannot protect against attacks from already-compromised nodes within a segment
- Requires accurate threat modeling to define appropriate boundaries
- Must balance security with functional requirements for legitimate cross-domain communication

Gateway firewalling is most effective when combined with other CAN security mechanisms like message authentication (CAN-MAC), intrusion detection systems, and secure ECU design.