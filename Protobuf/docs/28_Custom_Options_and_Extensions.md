# Custom Options and Extensions in Protocol Buffers

## Overview

Custom options in Protocol Buffers allow you to extend the `.proto` file syntax with metadata that can be read at runtime or used during code generation. This enables powerful capabilities like custom validation rules, documentation generation, ORM mappings, and framework-specific configurations without modifying the core protobuf language.

## Core Concepts

**Custom Options** are extensions to the descriptor messages that describe your `.proto` files. You can attach custom options to:
- File-level definitions
- Messages
- Fields
- Enums and enum values
- Services
- Methods
- Oneof definitions

**Extensions** (proto2 only) allow you to reserve field numbers in a message for third-party additions, though custom options are the preferred mechanism for metadata.

## Defining Custom Options

Custom options are defined by extending the appropriate descriptor option message from `google/protobuf/descriptor.proto`:

```protobuf
syntax = "proto3";

import "google/protobuf/descriptor.proto";

package mycompany.validation;

// Extend FieldOptions to add validation metadata
extend google.protobuf.FieldOptions {
  // Field-level validation rules
  int32 min_value = 50001;
  int32 max_value = 50002;
  string pattern = 50003;
  bool required = 50004;
}

// Extend MessageOptions for message-level metadata
extend google.protobuf.MessageOptions {
  string table_name = 50010;
  bool enable_caching = 50011;
}

// Extend MethodOptions for API metadata
extend google.protobuf.MethodOptions {
  string http_method = 50020;
  string http_path = 50021;
  bool requires_auth = 50022;
}
```

**Important**: Custom option field numbers must be in the range 50000-99999 (reserved for internal use within organizations) or 1000-536870911 (for public/shared options).

## Using Custom Options

Once defined, apply custom options using square bracket syntax:

```protobuf
syntax = "proto3";

import "validation.proto";

message User {
  option (mycompany.validation.table_name) = "users";
  option (mycompany.validation.enable_caching) = true;
  
  string email = 1 [
    (mycompany.validation.required) = true,
    (mycompany.validation.pattern) = "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$"
  ];
  
  int32 age = 2 [
    (mycompany.validation.min_value) = 0,
    (mycompany.validation.max_value) = 150
  ];
  
  string username = 3 [
    (mycompany.validation.required) = true
  ];
}

service UserService {
  rpc GetUser(GetUserRequest) returns (User) {
    option (mycompany.validation.http_method) = "GET";
    option (mycompany.validation.http_path) = "/api/users/{id}";
    option (mycompany.validation.requires_auth) = true;
  }
}
```

## C/C++ Implementation

### Reading Custom Options at Runtime

```cpp
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include "validation.pb.h"
#include "user.pb.h"
#include <iostream>

class ValidationEngine {
public:
    // Validate a message based on custom options
    static bool ValidateMessage(const google::protobuf::Message& message) {
        const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
        const google::protobuf::Reflection* reflection = message.GetReflection();
        
        std::cout << "Validating message: " << descriptor->name() << std::endl;
        
        // Check message-level options
        const google::protobuf::MessageOptions& msg_options = descriptor->options();
        if (msg_options.HasExtension(mycompany::validation::table_name)) {
            std::string table = msg_options.GetExtension(mycompany::validation::table_name);
            std::cout << "  Table name: " << table << std::endl;
        }
        
        // Validate each field
        for (int i = 0; i < descriptor->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = descriptor->field(i);
            if (!ValidateField(message, field, reflection)) {
                return false;
            }
        }
        
        return true;
    }
    
private:
    static bool ValidateField(const google::protobuf::Message& message,
                            const google::protobuf::FieldDescriptor* field,
                            const google::protobuf::Reflection* reflection) {
        const google::protobuf::FieldOptions& options = field->options();
        
        // Check required option
        if (options.HasExtension(mycompany::validation::required)) {
            bool is_required = options.GetExtension(mycompany::validation::required);
            if (is_required && !reflection->HasField(message, field)) {
                std::cerr << "Validation error: Required field '" 
                         << field->name() << "' is missing" << std::endl;
                return false;
            }
        }
        
        // Validate integer ranges
        if (field->type() == google::protobuf::FieldDescriptor::TYPE_INT32) {
            if (reflection->HasField(message, field)) {
                int32_t value = reflection->GetInt32(message, field);
                
                if (options.HasExtension(mycompany::validation::min_value)) {
                    int32_t min = options.GetExtension(mycompany::validation::min_value);
                    if (value < min) {
                        std::cerr << "Validation error: Field '" << field->name() 
                                 << "' value " << value << " is below minimum " << min << std::endl;
                        return false;
                    }
                }
                
                if (options.HasExtension(mycompany::validation::max_value)) {
                    int32_t max = options.GetExtension(mycompany::validation::max_value);
                    if (value > max) {
                        std::cerr << "Validation error: Field '" << field->name() 
                                 << "' value " << value << " exceeds maximum " << max << std::endl;
                        return false;
                    }
                }
            }
        }
        
        // Validate string patterns
        if (field->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
            if (options.HasExtension(mycompany::validation::pattern) && 
                reflection->HasField(message, field)) {
                std::string pattern = options.GetExtension(mycompany::validation::pattern);
                std::string value = reflection->GetString(message, field);
                
                // In production, use a regex library like std::regex or RE2
                std::cout << "  Would validate '" << value 
                         << "' against pattern: " << pattern << std::endl;
            }
        }
        
        return true;
    }
};

// Usage example
int main() {
    User user;
    user.set_email("user@example.com");
    user.set_age(25);
    user.set_username("john_doe");
    
    if (ValidationEngine::ValidateMessage(user)) {
        std::cout << "Validation passed!" << std::endl;
    } else {
        std::cout << "Validation failed!" << std::endl;
    }
    
    // Test with invalid data
    User invalid_user;
    invalid_user.set_age(200);  // Exceeds max_value
    
    ValidationEngine::ValidateMessage(invalid_user);
    
    return 0;
}
```

### Code Generation Plugin Using Custom Options

```cpp
// Custom protoc plugin that generates validation code
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

class ValidationCodeGenerator : public google::protobuf::compiler::CodeGenerator {
public:
    bool Generate(const google::protobuf::FileDescriptor* file,
                 const std::string& parameter,
                 google::protobuf::compiler::GeneratorContext* context,
                 std::string* error) const override {
        
        std::string output_filename = file->name();
        output_filename.replace(output_filename.size() - 6, 6, "_validation.h");
        
        std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> output(
            context->Open(output_filename));
        google::protobuf::io::Printer printer(output.get(), '$');
        
        printer.Print("#pragma once\n");
        printer.Print("#include \"$file$.pb.h\"\n\n", "file", 
                     file->name().substr(0, file->name().size() - 6));
        
        for (int i = 0; i < file->message_type_count(); ++i) {
            GenerateMessageValidator(file->message_type(i), &printer);
        }
        
        return true;
    }
    
private:
    void GenerateMessageValidator(const google::protobuf::Descriptor* descriptor,
                                  google::protobuf::io::Printer* printer) const {
        printer->Print("// Validator for $name$\n", "name", descriptor->name());
        printer->Print("bool Validate$name$(const $name$& msg) {\n",
                      "name", descriptor->name());
        printer->Indent();
        
        // Generate validation code based on custom options
        for (int i = 0; i < descriptor->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = descriptor->field(i);
            const google::protobuf::FieldOptions& options = field->options();
            
            if (options.HasExtension(mycompany::validation::required)) {
                printer->Print("if (!msg.has_$field$()) return false;\n",
                              "field", field->name());
            }
        }
        
        printer->Print("return true;\n");
        printer->Outdent();
        printer->Print("}\n\n");
    }
};
```

## Rust Implementation

### Reading Custom Options with prost-reflect

```rust
use prost::Message;
use prost_reflect::{DescriptorPool, DynamicMessage, ReflectMessage};
use prost_types::{DescriptorProto, FieldDescriptorProto, FileDescriptorSet};

// Define the validation options structure
#[derive(Clone, PartialEq, Message)]
pub struct FieldValidation {
    #[prost(int32, optional, tag = "50001")]
    pub min_value: Option<i32>,
    
    #[prost(int32, optional, tag = "50002")]
    pub max_value: Option<i32>,
    
    #[prost(string, optional, tag = "50003")]
    pub pattern: Option<String>,
    
    #[prost(bool, optional, tag = "50004")]
    pub required: Option<bool>,
}

#[derive(Clone, PartialEq, Message)]
pub struct MessageValidation {
    #[prost(string, optional, tag = "50010")]
    pub table_name: Option<String>,
    
    #[prost(bool, optional, tag = "50011")]
    pub enable_caching: Option<bool>,
}

// Validation engine using reflection
pub struct ValidationEngine {
    pool: DescriptorPool,
}

impl ValidationEngine {
    pub fn new(descriptor_set: &FileDescriptorSet) -> Result<Self, Box<dyn std::error::Error>> {
        let pool = DescriptorPool::from_file_descriptor_set(descriptor_set.clone())?;
        Ok(Self { pool })
    }
    
    pub fn validate_message(&self, message: &DynamicMessage) -> Result<(), String> {
        let descriptor = message.descriptor();
        
        println!("Validating message: {}", descriptor.name());
        
        // Read message-level options
        if let Some(options) = descriptor.options() {
            if let Ok(validation) = MessageValidation::decode(options.value()) {
                if let Some(table_name) = validation.table_name {
                    println!("  Table name: {}", table_name);
                }
            }
        }
        
        // Validate each field
        for field in descriptor.fields() {
            self.validate_field(message, &field)?;
        }
        
        Ok(())
    }
    
    fn validate_field(
        &self,
        message: &DynamicMessage,
        field: &prost_reflect::FieldDescriptor,
    ) -> Result<(), String> {
        // Read field options
        let options = match field.options() {
            Some(opts) => opts,
            None => return Ok(()),
        };
        
        // Parse validation options
        let validation = match FieldValidation::decode(options.value()) {
            Ok(v) => v,
            Err(_) => return Ok(()), // No validation options
        };
        
        // Check required
        if let Some(true) = validation.required {
            if !message.has_field(field) {
                return Err(format!("Required field '{}' is missing", field.name()));
            }
        }
        
        // Validate based on field type
        if message.has_field(field) {
            match field.kind() {
                prost_reflect::Kind::Int32 => {
                    if let Some(value) = message.get_field(field).as_i32() {
                        if let Some(min) = validation.min_value {
                            if value < min {
                                return Err(format!(
                                    "Field '{}' value {} is below minimum {}",
                                    field.name(), value, min
                                ));
                            }
                        }
                        
                        if let Some(max) = validation.max_value {
                            if value > max {
                                return Err(format!(
                                    "Field '{}' value {} exceeds maximum {}",
                                    field.name(), value, max
                                ));
                            }
                        }
                    }
                }
                prost_reflect::Kind::String => {
                    if let Some(pattern) = validation.pattern {
                        if let Some(value) = message.get_field(field).as_str() {
                            println!("  Would validate '{}' against pattern: {}", value, pattern);
                            // Use regex crate in production
                        }
                    }
                }
                _ => {}
            }
        }
        
        Ok(())
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_validation() {
        // In practice, load from compiled descriptor
        let descriptor_set = load_descriptor_set();
        let engine = ValidationEngine::new(&descriptor_set).unwrap();
        
        // Create a dynamic message
        let pool = engine.pool.clone();
        let message_desc = pool.get_message_by_name("User").unwrap();
        let mut message = DynamicMessage::new(message_desc);
        
        // Set valid fields
        message.set_field_by_name("email", "user@example.com".into());
        message.set_field_by_name("age", 25.into());
        message.set_field_by_name("username", "john_doe".into());
        
        assert!(engine.validate_message(&message).is_ok());
        
        // Test with invalid age
        message.set_field_by_name("age", 200.into());
        assert!(engine.validate_message(&message).is_err());
    }
    
    fn load_descriptor_set() -> FileDescriptorSet {
        // Load compiled descriptor from file or include_bytes!
        // This would typically be generated by protoc
        unimplemented!()
    }
}
```

### Procedural Macro for Compile-Time Validation

```rust
// In a separate proc-macro crate
use proc_macro::TokenStream;
use quote::quote;
use syn::{parse_macro_input, DeriveInput};

#[proc_macro_derive(Validate, attributes(validate))]
pub fn derive_validate(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let name = &input.ident;
    
    // Parse attributes to generate validation methods
    let gen = quote! {
        impl #name {
            pub fn validate(&self) -> Result<(), ValidationError> {
                // Generated validation logic based on custom options
                Ok(())
            }
        }
    };
    
    gen.into()
}
```

## Summary

**Custom options and extensions in Protocol Buffers provide a powerful mechanism for adding metadata to your schema definitions.** Key takeaways:

- **Custom options** extend descriptor messages to attach metadata to files, messages, fields, services, and methods
- **Field numbers 50000-99999** are reserved for internal organizational use
- **Runtime reflection** in C++ and Rust allows reading custom options to implement validation, code generation, and framework integration
- **C++ implementation** uses the descriptor and reflection APIs to traverse messages and read extension values
- **Rust implementation** leverages `prost-reflect` for dynamic message inspection and custom option parsing
- **Common use cases** include validation rules, ORM mappings, API routing metadata, documentation generation, and serialization hints
- **Code generation plugins** can consume custom options to generate language-specific validation, serialization, or framework code
- Custom options enable **framework-agnostic schema definitions** that carry their own metadata without coupling to specific languages or tools

This approach allows schema definitions to be self-documenting and carry validation logic, API specifications, and other metadata directly within the `.proto` files, which can then be leveraged by runtime systems or code generators across any language that supports Protocol Buffers.