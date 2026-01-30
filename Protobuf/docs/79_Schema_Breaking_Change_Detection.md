# Schema Breaking Change Detection in Protocol Buffers

## Detailed Description

Schema breaking change detection is a critical practice in Protocol Buffers development that involves automatically identifying incompatible modifications to `.proto` files that could break backwards or forwards compatibility. This process is typically integrated into CI/CD pipelines to prevent deployment of changes that would cause runtime failures, data corruption, or service disruptions in distributed systems.

### Why Schema Breaking Change Detection Matters

In microservices architectures and distributed systems, services often communicate using Protocol Buffers. When multiple services depend on the same `.proto` definitions, or when older clients need to communicate with newer servers (and vice versa), maintaining schema compatibility becomes crucial. A breaking change can cause:

- **Deserialization failures** when old code tries to read new messages
- **Data loss** when required fields are removed
- **Type mismatches** when field types change
- **Wire format incompatibilities** when field numbers are reused

### Types of Breaking Changes

**Breaking Changes:**
- Removing or renaming fields
- Changing field numbers
- Changing field types (especially incompatible types)
- Changing a field between `optional`, `required`, and `repeated`
- Removing or renaming messages or enums
- Changing enum values
- Moving messages between packages

**Non-Breaking Changes:**
- Adding new optional fields
- Adding new messages or enums
- Adding new enum values (with care)
- Adding new RPC methods
- Deprecating fields (without removing them)

## C/C++ Implementation

### Using buf for Schema Validation

```cpp
// schema_validator.h
#ifndef SCHEMA_VALIDATOR_H
#define SCHEMA_VALIDATOR_H

#include <string>
#include <vector>
#include <memory>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/compiler/importer.h>

namespace schema_validation {

enum class ChangeType {
    FIELD_REMOVED,
    FIELD_NUMBER_CHANGED,
    FIELD_TYPE_CHANGED,
    FIELD_LABEL_CHANGED,
    MESSAGE_REMOVED,
    ENUM_REMOVED,
    PACKAGE_CHANGED
};

struct BreakingChange {
    ChangeType type;
    std::string location;
    std::string description;
    std::string old_value;
    std::string new_value;
};

class SchemaValidator {
public:
    SchemaValidator() = default;
    
    // Load schema from descriptor set
    bool LoadOldSchema(const std::string& descriptor_set_path);
    bool LoadNewSchema(const std::string& descriptor_set_path);
    
    // Detect breaking changes
    std::vector<BreakingChange> DetectBreakingChanges();
    
private:
    std::unique_ptr<google::protobuf::DescriptorPool> old_pool_;
    std::unique_ptr<google::protobuf::DescriptorPool> new_pool_;
    
    void CompareMessages(const google::protobuf::Descriptor* old_msg,
                        const google::protobuf::Descriptor* new_msg,
                        std::vector<BreakingChange>& changes);
    
    void CompareFields(const google::protobuf::FieldDescriptor* old_field,
                      const google::protobuf::FieldDescriptor* new_field,
                      const std::string& message_name,
                      std::vector<BreakingChange>& changes);
};

} // namespace schema_validation

#endif // SCHEMA_VALIDATOR_H
```

```cpp
// schema_validator.cpp
#include "schema_validator.h"
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <fstream>
#include <sstream>

namespace schema_validation {

bool SchemaValidator::LoadOldSchema(const std::string& descriptor_set_path) {
    std::ifstream input(descriptor_set_path, std::ios::binary);
    if (!input) {
        return false;
    }
    
    google::protobuf::FileDescriptorSet file_set;
    if (!file_set.ParseFromIstream(&input)) {
        return false;
    }
    
    old_pool_ = std::make_unique<google::protobuf::DescriptorPool>();
    
    for (const auto& file_proto : file_set.file()) {
        old_pool_->BuildFile(file_proto);
    }
    
    return true;
}

bool SchemaValidator::LoadNewSchema(const std::string& descriptor_set_path) {
    std::ifstream input(descriptor_set_path, std::ios::binary);
    if (!input) {
        return false;
    }
    
    google::protobuf::FileDescriptorSet file_set;
    if (!file_set.ParseFromIstream(&input)) {
        return false;
    }
    
    new_pool_ = std::make_unique<google::protobuf::DescriptorPool>();
    
    for (const auto& file_proto : file_set.file()) {
        new_pool_->BuildFile(file_proto);
    }
    
    return true;
}

std::vector<BreakingChange> SchemaValidator::DetectBreakingChanges() {
    std::vector<BreakingChange> changes;
    
    if (!old_pool_ || !new_pool_) {
        return changes;
    }
    
    // Get all message types from old schema
    std::vector<const google::protobuf::Descriptor*> old_messages;
    for (int i = 0; i < old_pool_->pool()->file_count(); ++i) {
        const auto* file = old_pool_->pool()->file(i);
        for (int j = 0; j < file->message_type_count(); ++j) {
            old_messages.push_back(file->message_type(j));
        }
    }
    
    // Compare each message
    for (const auto* old_msg : old_messages) {
        const auto* new_msg = new_pool_->FindMessageTypeByName(old_msg->full_name());
        
        if (!new_msg) {
            changes.push_back({
                ChangeType::MESSAGE_REMOVED,
                old_msg->full_name(),
                "Message was removed",
                old_msg->full_name(),
                ""
            });
        } else {
            CompareMessages(old_msg, new_msg, changes);
        }
    }
    
    return changes;
}

void SchemaValidator::CompareMessages(
    const google::protobuf::Descriptor* old_msg,
    const google::protobuf::Descriptor* new_msg,
    std::vector<BreakingChange>& changes) {
    
    // Check package change
    if (old_msg->file()->package() != new_msg->file()->package()) {
        changes.push_back({
            ChangeType::PACKAGE_CHANGED,
            old_msg->full_name(),
            "Package changed",
            old_msg->file()->package(),
            new_msg->file()->package()
        });
    }
    
    // Check all fields in old message
    for (int i = 0; i < old_msg->field_count(); ++i) {
        const auto* old_field = old_msg->field(i);
        const auto* new_field = new_msg->FindFieldByNumber(old_field->number());
        
        if (!new_field) {
            changes.push_back({
                ChangeType::FIELD_REMOVED,
                old_msg->full_name() + "." + old_field->name(),
                "Field was removed",
                old_field->name(),
                ""
            });
        } else {
            CompareFields(old_field, new_field, old_msg->full_name(), changes);
        }
    }
}

void SchemaValidator::CompareFields(
    const google::protobuf::FieldDescriptor* old_field,
    const google::protobuf::FieldDescriptor* new_field,
    const std::string& message_name,
    std::vector<BreakingChange>& changes) {
    
    std::string field_location = message_name + "." + old_field->name();
    
    // Check field number change
    if (old_field->number() != new_field->number()) {
        changes.push_back({
            ChangeType::FIELD_NUMBER_CHANGED,
            field_location,
            "Field number changed",
            std::to_string(old_field->number()),
            std::to_string(new_field->number())
        });
    }
    
    // Check type change
    if (old_field->type() != new_field->type()) {
        changes.push_back({
            ChangeType::FIELD_TYPE_CHANGED,
            field_location,
            "Field type changed",
            old_field->type_name(),
            new_field->type_name()
        });
    }
    
    // Check label change (optional, required, repeated)
    if (old_field->label() != new_field->label()) {
        changes.push_back({
            ChangeType::FIELD_LABEL_CHANGED,
            field_location,
            "Field label changed",
            google::protobuf::FieldDescriptor::kLabelNames[old_field->label()],
            google::protobuf::FieldDescriptor::kLabelNames[new_field->label()]
        });
    }
}

} // namespace schema_validation
```

```cpp
// main.cpp - CI/CD Integration Example
#include "schema_validator.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] 
                  << " <old_descriptor_set> <new_descriptor_set>" << std::endl;
        return 1;
    }
    
    schema_validation::SchemaValidator validator;
    
    if (!validator.LoadOldSchema(argv[1])) {
        std::cerr << "Failed to load old schema" << std::endl;
        return 1;
    }
    
    if (!validator.LoadNewSchema(argv[2])) {
        std::cerr << "Failed to load new schema" << std::endl;
        return 1;
    }
    
    auto breaking_changes = validator.DetectBreakingChanges();
    
    if (breaking_changes.empty()) {
        std::cout << "✓ No breaking changes detected" << std::endl;
        return 0;
    }
    
    std::cout << "✗ Breaking changes detected:" << std::endl;
    for (const auto& change : breaking_changes) {
        std::cout << "  - " << change.location << ": " 
                  << change.description << std::endl;
        if (!change.old_value.empty()) {
            std::cout << "    Old: " << change.old_value << std::endl;
            std::cout << "    New: " << change.new_value << std::endl;
        }
    }
    
    return 1; // Exit with error to fail CI/CD
}
```

## Rust Implementation

```rust
// lib.rs
use prost_types::{FileDescriptorSet, DescriptorProto, FieldDescriptorProto};
use std::collections::HashMap;
use std::fs;
use std::io::Read;

#[derive(Debug, Clone, PartialEq)]
pub enum ChangeType {
    FieldRemoved,
    FieldNumberChanged,
    FieldTypeChanged,
    FieldLabelChanged,
    MessageRemoved,
    EnumRemoved,
    PackageChanged,
}

#[derive(Debug, Clone)]
pub struct BreakingChange {
    pub change_type: ChangeType,
    pub location: String,
    pub description: String,
    pub old_value: Option<String>,
    pub new_value: Option<String>,
}

pub struct SchemaValidator {
    old_descriptors: Option<FileDescriptorSet>,
    new_descriptors: Option<FileDescriptorSet>,
}

impl SchemaValidator {
    pub fn new() -> Self {
        Self {
            old_descriptors: None,
            new_descriptors: None,
        }
    }
    
    pub fn load_old_schema(&mut self, path: &str) -> Result<(), Box<dyn std::error::Error>> {
        let mut file = fs::File::open(path)?;
        let mut buffer = Vec::new();
        file.read_to_end(&mut buffer)?;
        
        self.old_descriptors = Some(prost::Message::decode(&buffer[..])?);
        Ok(())
    }
    
    pub fn load_new_schema(&mut self, path: &str) -> Result<(), Box<dyn std::error::Error>> {
        let mut file = fs::File::open(path)?;
        let mut buffer = Vec::new();
        file.read_to_end(&mut buffer)?;
        
        self.new_descriptors = Some(prost::Message::decode(&buffer[..])?);
        Ok(())
    }
    
    pub fn detect_breaking_changes(&self) -> Vec<BreakingChange> {
        let mut changes = Vec::new();
        
        let old_descriptors = match &self.old_descriptors {
            Some(d) => d,
            None => return changes,
        };
        
        let new_descriptors = match &self.new_descriptors {
            Some(d) => d,
            None => return changes,
        };
        
        // Build message lookup maps
        let old_messages = self.build_message_map(old_descriptors);
        let new_messages = self.build_message_map(new_descriptors);
        
        // Check for removed messages and field changes
        for (name, old_msg) in &old_messages {
            match new_messages.get(name) {
                None => {
                    changes.push(BreakingChange {
                        change_type: ChangeType::MessageRemoved,
                        location: name.clone(),
                        description: "Message was removed".to_string(),
                        old_value: Some(name.clone()),
                        new_value: None,
                    });
                }
                Some(new_msg) => {
                    self.compare_messages(name, old_msg, new_msg, &mut changes);
                }
            }
        }
        
        changes
    }
    
    fn build_message_map(&self, descriptors: &FileDescriptorSet) 
        -> HashMap<String, DescriptorProto> {
        let mut map = HashMap::new();
        
        for file in &descriptors.file {
            let package = file.package.as_ref().map(|s| s.as_str()).unwrap_or("");
            
            for message in &file.message_type {
                let full_name = if package.is_empty() {
                    message.name.clone().unwrap_or_default()
                } else {
                    format!("{}.{}", package, message.name.as_ref().unwrap_or(&String::new()))
                };
                map.insert(full_name, message.clone());
            }
        }
        
        map
    }
    
    fn compare_messages(
        &self,
        name: &str,
        old_msg: &DescriptorProto,
        new_msg: &DescriptorProto,
        changes: &mut Vec<BreakingChange>,
    ) {
        // Build field maps by number
        let old_fields: HashMap<i32, &FieldDescriptorProto> = old_msg
            .field
            .iter()
            .filter_map(|f| f.number.map(|n| (n, f)))
            .collect();
        
        let new_fields: HashMap<i32, &FieldDescriptorProto> = new_msg
            .field
            .iter()
            .filter_map(|f| f.number.map(|n| (n, f)))
            .collect();
        
        // Check for removed or changed fields
        for (number, old_field) in &old_fields {
            let field_name = old_field.name.as_ref().unwrap_or(&String::new());
            let location = format!("{}.{}", name, field_name);
            
            match new_fields.get(number) {
                None => {
                    changes.push(BreakingChange {
                        change_type: ChangeType::FieldRemoved,
                        location,
                        description: "Field was removed".to_string(),
                        old_value: Some(field_name.clone()),
                        new_value: None,
                    });
                }
                Some(new_field) => {
                    self.compare_fields(&location, old_field, new_field, changes);
                }
            }
        }
    }
    
    fn compare_fields(
        &self,
        location: &str,
        old_field: &FieldDescriptorProto,
        new_field: &FieldDescriptorProto,
        changes: &mut Vec<BreakingChange>,
    ) {
        // Check type change
        if old_field.r#type != new_field.r#type {
            changes.push(BreakingChange {
                change_type: ChangeType::FieldTypeChanged,
                location: location.to_string(),
                description: "Field type changed".to_string(),
                old_value: Some(format!("{:?}", old_field.r#type)),
                new_value: Some(format!("{:?}", new_field.r#type)),
            });
        }
        
        // Check label change (optional, required, repeated)
        if old_field.label != new_field.label {
            changes.push(BreakingChange {
                change_type: ChangeType::FieldLabelChanged,
                location: location.to_string(),
                description: "Field label changed".to_string(),
                old_value: Some(format!("{:?}", old_field.label)),
                new_value: Some(format!("{:?}", new_field.label)),
            });
        }
    }
}

impl Default for SchemaValidator {
    fn default() -> Self {
        Self::new()
    }
}
```

```rust
// main.rs - CLI Tool for CI/CD
use std::env;
use std::process;

mod schema_validator;
use schema_validator::{SchemaValidator, ChangeType};

fn main() {
    let args: Vec<String> = env::args().collect();
    
    if args.len() != 3 {
        eprintln!("Usage: {} <old_descriptor_set> <new_descriptor_set>", args[0]);
        process::exit(1);
    }
    
    let mut validator = SchemaValidator::new();
    
    if let Err(e) = validator.load_old_schema(&args[1]) {
        eprintln!("Failed to load old schema: {}", e);
        process::exit(1);
    }
    
    if let Err(e) = validator.load_new_schema(&args[2]) {
        eprintln!("Failed to load new schema: {}", e);
        process::exit(1);
    }
    
    let breaking_changes = validator.detect_breaking_changes();
    
    if breaking_changes.is_empty() {
        println!("✓ No breaking changes detected");
        process::exit(0);
    }
    
    println!("✗ Breaking changes detected:");
    for change in &breaking_changes {
        println!("  - {}: {}", change.location, change.description);
        if let Some(old_val) = &change.old_value {
            println!("    Old: {}", old_val);
        }
        if let Some(new_val) = &change.new_value {
            println!("    New: {}", new_val);
        }
    }
    
    process::exit(1);
}
```

```toml
# Cargo.toml
[package]
name = "protobuf-schema-validator"
version = "0.1.0"
edition = "2021"

[dependencies]
prost = "0.12"
prost-types = "0.12"

[dev-dependencies]
prost-build = "0.12"
```

## CI/CD Integration Examples

### GitHub Actions Workflow

```yaml
# .github/workflows/proto-breaking-changes.yml
name: Protobuf Breaking Change Detection

on:
  pull_request:
    paths:
      - '**/*.proto'

jobs:
  check-breaking-changes:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
        with:
          fetch-depth: 0
      
      - name: Install Buf
        run: |
          BUF_VERSION=1.28.1
          curl -sSL \
            "https://github.com/bufbuild/buf/releases/download/v${BUF_VERSION}/buf-$(uname -s)-$(uname -m)" \
            -o /usr/local/bin/buf
          chmod +x /usr/local/bin/buf
      
      - name: Check for breaking changes
        run: |
          buf breaking --against '.git#branch=main'
```

### Using Buf Configuration

```yaml
# buf.yaml
version: v1
breaking:
  use:
    - FILE
  except:
    - FIELD_SAME_JSON_NAME
lint:
  use:
    - DEFAULT
```

## Summary

**Schema Breaking Change Detection** is an essential practice for maintaining Protocol Buffer compatibility in production systems. It automates the identification of incompatible schema modifications before they cause runtime failures.

**Key Capabilities:**
- Detects field removals, type changes, and number reassignments
- Identifies message/enum deletions and package modifications
- Validates backwards and forwards compatibility
- Integrates seamlessly into CI/CD pipelines

**Implementation Approaches:**
- **C++**: Direct descriptor comparison using Protocol Buffer reflection API
- **Rust**: Type-safe validation using prost-types for descriptor analysis
- **Tools**: Buf CLI for production-grade breaking change detection

**Best Practices:**
- Run validation on every pull request affecting `.proto` files
- Fail builds when breaking changes are detected without approval
- Use semantic versioning to communicate compatibility guarantees
- Maintain descriptor sets for historical schema comparison
- Document allowed breaking changes with proper migration paths

By implementing automated schema validation, teams can confidently evolve their Protocol Buffer definitions while maintaining system reliability and preventing costly runtime errors in distributed environments.