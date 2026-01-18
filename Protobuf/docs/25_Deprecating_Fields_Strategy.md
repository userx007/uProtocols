# Deprecating Fields in Protocol Buffers

## Overview

Field deprecation in Protocol Buffers is a crucial strategy for evolving schemas while maintaining backward compatibility. The `deprecated = true` option marks fields that should no longer be used, signaling to developers that they should migrate away from these fields without breaking existing code that still relies on them.

## Why Deprecate Fields?

- **Schema Evolution**: Business requirements change, and data models need to adapt
- **API Evolution**: Gradual migration from old to new field designs
- **Backward Compatibility**: Allow existing clients to continue functioning while encouraging migration
- **Clear Communication**: Explicitly signal to developers which fields are obsolete
- **Safe Removal Path**: Provides a transitional period before complete field removal

## The Deprecated Option

The `deprecated` option is a field-level annotation that generates compiler warnings when the field is accessed in generated code.

```protobuf
syntax = "proto3";

message UserProfile {
  string user_id = 1;
  
  // Old field - deprecated
  string full_name = 2 [deprecated = true];
  
  // New fields - preferred approach
  string first_name = 3;
  string last_name = 4;
  
  // Deprecated nested structure
  Address old_address = 5 [deprecated = true];
  DetailedAddress new_address = 6;
}

message Address {
  string street = 1;
  string city = 2;
}

message DetailedAddress {
  string street_line1 = 1;
  string street_line2 = 2;
  string city = 3;
  string state = 4;
  string postal_code = 5;
  string country = 6;
}
```

## C/C++ Examples

### Basic Deprecation Usage

```c
#include <stdio.h>
#include "user_profile.pb-c.h"

void create_user_with_deprecated_field() {
    UserProfile user = USER_PROFILE__INIT;
    
    user.user_id = "user123";
    
    // Using deprecated field - compiler may warn
    // Warning: 'full_name' is deprecated
    user.full_name = "John Doe";
    
    // Preferred approach - use new fields
    user.first_name = "John";
    user.last_name = "Doe";
    
    printf("User: %s %s\n", user.first_name, user.last_name);
}

void migrate_from_deprecated() {
    UserProfile user = USER_PROFILE__INIT;
    
    // During migration, read from deprecated field if new fields empty
    if (user.has_full_name && user.full_name != NULL) {
        // Parse full_name and populate new fields
        // This is a migration path
        printf("Migrating from deprecated full_name: %s\n", user.full_name);
        
        // Split logic here...
        user.first_name = "John";
        user.last_name = "Doe";
    }
}
```

### C++ Advanced Example

```cpp
#include <iostream>
#include <string>
#include "user_profile.pb.h"

class UserProfileManager {
public:
    // Migration helper function
    static void MigrateProfile(UserProfile& profile) {
        // If deprecated field has data but new fields don't
        if (profile.has_full_name() && 
            !profile.has_first_name() && 
            !profile.has_last_name()) {
            
            std::string full_name = profile.full_name();
            size_t space_pos = full_name.find(' ');
            
            if (space_pos != std::string::npos) {
                profile.set_first_name(full_name.substr(0, space_pos));
                profile.set_last_name(full_name.substr(space_pos + 1));
            } else {
                profile.set_first_name(full_name);
            }
            
            // Optionally clear deprecated field after migration
            // profile.clear_full_name();
        }
    }
    
    // Writing with new fields only
    static void CreateModernProfile(UserProfile& profile, 
                                   const std::string& user_id,
                                   const std::string& first,
                                   const std::string& last) {
        profile.set_user_id(user_id);
        profile.set_first_name(first);
        profile.set_last_name(last);
        
        // Don't use deprecated fields in new code
        // profile.set_full_name(...); // Avoid this!
    }
    
    // Safe reading that handles both old and new formats
    static std::string GetDisplayName(const UserProfile& profile) {
        if (profile.has_first_name() && profile.has_last_name()) {
            return profile.first_name() + " " + profile.last_name();
        } else if (profile.has_full_name()) {
            // Fallback to deprecated field for old data
            return profile.full_name();
        }
        return "Unknown";
    }
};

int main() {
    UserProfile profile;
    
    // Modern approach
    UserProfileManager::CreateModernProfile(profile, "u123", "Jane", "Smith");
    
    std::cout << "Display name: " 
              << UserProfileManager::GetDisplayName(profile) << std::endl;
    
    // Simulate old data
    UserProfile old_profile;
    old_profile.set_user_id("u456");
    old_profile.set_full_name("Bob Johnson");  // Deprecated usage
    
    // Migrate old data
    UserProfileManager::MigrateProfile(old_profile);
    
    std::cout << "Migrated: " << old_profile.first_name() 
              << " " << old_profile.last_name() << std::endl;
    
    return 0;
}
```

## Rust Examples

### Basic Deprecation

```rust
// Generated code will include deprecation warnings
use user_profile::UserProfile;

fn create_user_modern() {
    let mut user = UserProfile::default();
    user.user_id = "user789".to_string();
    
    // Preferred approach - use new fields
    user.first_name = "Alice".to_string();
    user.last_name = "Williams".to_string();
    
    println!("User: {} {}", user.first_name, user.last_name);
}

#[allow(deprecated)]
fn create_user_deprecated() {
    let mut user = UserProfile::default();
    user.user_id = "user456".to_string();
    
    // Using deprecated field - triggers warning
    user.full_name = "Bob Smith".to_string();
    
    println!("User (deprecated): {}", user.full_name);
}
```

### Migration Strategy in Rust

```rust
use user_profile::{UserProfile, Address, DetailedAddress};

struct ProfileMigrator;

impl ProfileMigrator {
    /// Migrate from deprecated full_name to first_name/last_name
    #[allow(deprecated)]
    pub fn migrate_name(profile: &mut UserProfile) {
        if !profile.full_name.is_empty() 
            && profile.first_name.is_empty() 
            && profile.last_name.is_empty() {
            
            let parts: Vec<&str> = profile.full_name.splitn(2, ' ').collect();
            
            match parts.as_slice() {
                [first, last] => {
                    profile.first_name = first.to_string();
                    profile.last_name = last.to_string();
                }
                [single] => {
                    profile.first_name = single.to_string();
                }
                _ => {}
            }
            
            // Optionally clear deprecated field
            // profile.full_name.clear();
        }
    }
    
    /// Migrate from old Address to new DetailedAddress
    #[allow(deprecated)]
    pub fn migrate_address(profile: &mut UserProfile) {
        if profile.old_address.is_some() && profile.new_address.is_none() {
            if let Some(old_addr) = &profile.old_address {
                let mut new_addr = DetailedAddress::default();
                new_addr.street_line1 = old_addr.street.clone();
                new_addr.city = old_addr.city.clone();
                
                profile.new_address = Some(new_addr);
            }
        }
    }
    
    /// Complete migration of profile
    pub fn migrate_all(profile: &mut UserProfile) {
        Self::migrate_name(profile);
        Self::migrate_address(profile);
    }
}

/// Safe accessor that handles both old and new formats
pub fn get_display_name(profile: &UserProfile) -> String {
    if !profile.first_name.is_empty() || !profile.last_name.is_empty() {
        format!("{} {}", profile.first_name, profile.last_name).trim().to_string()
    } else {
        #[allow(deprecated)]
        {
            profile.full_name.clone()
        }
    }
}

/// Safe accessor for address
#[allow(deprecated)]
pub fn get_city(profile: &UserProfile) -> Option<String> {
    if let Some(addr) = &profile.new_address {
        Some(addr.city.clone())
    } else if let Some(old_addr) = &profile.old_address {
        Some(old_addr.city.clone())
    } else {
        None
    }
}

fn main() {
    // Example 1: Creating modern profile
    let mut modern_profile = UserProfile {
        user_id: "u001".to_string(),
        first_name: "Sarah".to_string(),
        last_name: "Connor".to_string(),
        ..Default::default()
    };
    
    println!("Modern: {}", get_display_name(&modern_profile));
    
    // Example 2: Migrating old profile
    #[allow(deprecated)]
    let mut old_profile = UserProfile {
        user_id: "u002".to_string(),
        full_name: "John Connor".to_string(),
        ..Default::default()
    };
    
    println!("Before migration: {}", get_display_name(&old_profile));
    ProfileMigrator::migrate_all(&mut old_profile);
    println!("After migration: {} {}", 
             old_profile.first_name, old_profile.last_name);
}
```

### Batch Migration Example

```rust
use std::collections::HashMap;

pub struct BatchMigrator {
    migration_stats: HashMap<String, usize>,
}

impl BatchMigrator {
    pub fn new() -> Self {
        Self {
            migration_stats: HashMap::new(),
        }
    }
    
    pub fn migrate_profiles(&mut self, profiles: &mut [UserProfile]) {
        for profile in profiles.iter_mut() {
            let mut migrated_fields = Vec::new();
            
            #[allow(deprecated)]
            {
                if !profile.full_name.is_empty() && 
                   profile.first_name.is_empty() {
                    ProfileMigrator::migrate_name(profile);
                    migrated_fields.push("full_name");
                }
                
                if profile.old_address.is_some() && 
                   profile.new_address.is_none() {
                    ProfileMigrator::migrate_address(profile);
                    migrated_fields.push("old_address");
                }
            }
            
            for field in migrated_fields {
                *self.migration_stats.entry(field.to_string())
                    .or_insert(0) += 1;
            }
        }
    }
    
    pub fn print_stats(&self) {
        println!("Migration Statistics:");
        for (field, count) in &self.migration_stats {
            println!("  {}: {} profiles migrated", field, count);
        }
    }
}
```

## Migration Strategy Best Practices

### 1. **Gradual Deprecation Path**

```protobuf
// Phase 1: Add new fields, keep old
message Product {
  string id = 1;
  double price = 2 [deprecated = true];  // Old: single currency
  Money price_details = 3;  // New: multi-currency support
}

message Money {
  double amount = 1;
  string currency = 2;
}
```

### 2. **Dual-Write Pattern**

During migration, write to both old and new fields:

```cpp
void UpdateProduct(Product& product, double amount, const string& currency) {
    // Write to new field (preferred)
    product.mutable_price_details()->set_amount(amount);
    product.mutable_price_details()->set_currency(currency);
    
    // Also write to deprecated field for backward compatibility
    product.set_price(amount);  // Deprecated but still populated
}
```

### 3. **Dual-Read Pattern**

Read from new field first, fall back to deprecated:

```rust
fn get_price(product: &Product) -> (f64, String) {
    if let Some(price_details) = &product.price_details {
        (price_details.amount, price_details.currency.clone())
    } else {
        #[allow(deprecated)]
        {
            (product.price, "USD".to_string())
        }
    }
}
```

### 4. **Complete Removal Timeline**

1. **T0**: Add new fields, mark old as deprecated
2. **T1**: Update all writers to dual-write
3. **T2**: Migrate existing data
4. **T3**: Update readers to prefer new fields
5. **T4**: Stop writing to deprecated fields
6. **T5**: Reserve deprecated field numbers (prevent reuse)

```protobuf
message Product {
  reserved 2;  // Was: double price [deprecated = true];
  reserved "price";
  
  string id = 1;
  Money price_details = 3;
}
```

## Summary

**Field deprecation** in Protocol Buffers is a controlled approach to schema evolution that:

- **Maintains Compatibility**: Existing code continues to work while discouraging new usage
- **Signals Intent**: Clearly communicates to developers that fields are obsolete
- **Enables Safe Migration**: Provides transition period for updating all systems
- **Generates Warnings**: Compiler/linter warnings help catch deprecated field usage
- **Supports Versioning**: Allows different service versions to coexist during rollout

**Key Strategies**:
- Use `[deprecated = true]` to mark fields
- Implement dual-read/dual-write during transition
- Create migration utilities for batch data updates
- Use `#[allow(deprecated)]` or `#pragma warning` to suppress warnings during migration
- Eventually reserve deprecated field numbers to prevent reuse
- Document the migration path clearly for API consumers

Deprecation is not removal—it's a communication mechanism that allows graceful evolution of your data models while respecting existing integrations and providing clear upgrade paths.