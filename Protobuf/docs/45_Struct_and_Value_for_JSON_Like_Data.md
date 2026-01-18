# Struct and Value for JSON-Like Data in Protocol Buffers

## Overview

Protocol Buffers are traditionally schema-based, requiring predefined message structures. However, real-world applications often need to handle dynamic, schemaless data similar to JSON. Google provides special well-known types in `google/protobuf/struct.proto` specifically for this purpose: `Struct`, `Value`, `ListValue`, and `NullValue`.

These types allow you to represent arbitrary JSON-like data structures within Protobuf messages, making them invaluable for:
- APIs that accept flexible input formats
- Configuration systems with varying schemas
- Bridge protocols between JSON and Protobuf systems
- Storing user-defined metadata
- Dynamic form data or settings

## The Well-Known Types

### Core Types in google/protobuf/struct.proto

**1. Struct**: Represents a JSON object (key-value map)
```protobuf
message Struct {
  map<string, Value> fields = 1;
}
```

**2. Value**: Represents any JSON value (polymorphic wrapper)
```protobuf
message Value {
  oneof kind {
    NullValue null_value = 1;
    double number_value = 2;
    string string_value = 3;
    bool bool_value = 4;
    Struct struct_value = 5;
    ListValue list_value = 6;
  }
}
```

**3. ListValue**: Represents a JSON array
```protobuf
message ListValue {
  repeated Value values = 1;
}
```

**4. NullValue**: Represents JSON null
```protobuf
enum NullValue {
  NULL_VALUE = 0;
}
```

## Usage Example

Here's a practical example showing how to use these types in a message definition:

```protobuf
syntax = "proto3";

import "google/protobuf/struct.proto";

message UserProfile {
  string user_id = 1;
  string username = 2;
  
  // Dynamic metadata that can contain any JSON-like structure
  google.protobuf.Struct metadata = 3;
  
  // User preferences as a flexible value
  google.protobuf.Value preferences = 4;
  
  // List of dynamic tags
  google.protobuf.ListValue tags = 5;
}
```

## C/C++ Implementation

### Building and Accessing Struct Data

```cpp
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <memory>

using google::protobuf::Struct;
using google::protobuf::Value;
using google::protobuf::ListValue;
using google::protobuf::NULL_VALUE;

// Creating a Struct programmatically
Struct create_user_metadata() {
    Struct metadata;
    auto& fields = *metadata.mutable_fields();
    
    // Add string value
    fields["name"].set_string_value("John Doe");
    
    // Add number value
    fields["age"].set_number_value(30);
    
    // Add boolean value
    fields["is_active"].set_bool_value(true);
    
    // Add null value
    fields["avatar"].set_null_value(NULL_VALUE);
    
    // Add nested struct
    Struct address;
    auto& addr_fields = *address.mutable_fields();
    addr_fields["street"].set_string_value("123 Main St");
    addr_fields["city"].set_string_value("Springfield");
    addr_fields["zip"].set_number_value(12345);
    *fields["address"].mutable_struct_value() = address;
    
    // Add array/list
    ListValue hobbies;
    hobbies.add_values()->set_string_value("reading");
    hobbies.add_values()->set_string_value("coding");
    hobbies.add_values()->set_string_value("hiking");
    *fields["hobbies"].mutable_list_value() = hobbies;
    
    return metadata;
}

// Reading Struct data
void read_struct_data(const Struct& metadata) {
    const auto& fields = metadata.fields();
    
    // Access string value
    if (fields.contains("name")) {
        std::cout << "Name: " << fields.at("name").string_value() << std::endl;
    }
    
    // Access number value
    if (fields.contains("age")) {
        std::cout << "Age: " << fields.at("age").number_value() << std::endl;
    }
    
    // Access boolean value
    if (fields.contains("is_active")) {
        std::cout << "Active: " << (fields.at("is_active").bool_value() ? "yes" : "no") << std::endl;
    }
    
    // Check for null
    if (fields.contains("avatar") && 
        fields.at("avatar").kind_case() == Value::kNullValue) {
        std::cout << "Avatar: null" << std::endl;
    }
    
    // Access nested struct
    if (fields.contains("address") && 
        fields.at("address").kind_case() == Value::kStructValue) {
        const auto& addr = fields.at("address").struct_value().fields();
        std::cout << "Address: " << addr.at("street").string_value() 
                  << ", " << addr.at("city").string_value() << std::endl;
    }
    
    // Access list
    if (fields.contains("hobbies") && 
        fields.at("hobbies").kind_case() == Value::kListValue) {
        std::cout << "Hobbies: ";
        for (const auto& hobby : fields.at("hobbies").list_value().values()) {
            std::cout << hobby.string_value() << " ";
        }
        std::cout << std::endl;
    }
}

// Convert JSON string to Struct
bool json_to_struct(const std::string& json_str, Struct* out_struct) {
    google::protobuf::util::JsonParseOptions options;
    auto status = google::protobuf::util::JsonStringToMessage(
        json_str, out_struct, options);
    return status.ok();
}

// Convert Struct to JSON string
std::string struct_to_json(const Struct& struct_data) {
    std::string json_output;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.preserve_proto_field_names = true;
    
    google::protobuf::util::MessageToJsonString(
        struct_data, &json_output, options);
    return json_output;
}

int main() {
    // Create and populate struct
    Struct metadata = create_user_metadata();
    
    // Read the data
    std::cout << "=== Reading Struct Data ===" << std::endl;
    read_struct_data(metadata);
    
    // Convert to JSON
    std::cout << "\n=== Struct as JSON ===" << std::endl;
    std::cout << struct_to_json(metadata) << std::endl;
    
    // Parse JSON into Struct
    std::cout << "\n=== Parsing JSON to Struct ===" << std::endl;
    std::string json_input = R"({
        "username": "alice",
        "score": 95.5,
        "verified": true,
        "settings": {
            "theme": "dark",
            "notifications": true
        }
    })";
    
    Struct parsed_struct;
    if (json_to_struct(json_input, &parsed_struct)) {
        read_struct_data(parsed_struct);
    }
    
    return 0;
}
```

### Working with Value Type Directly

```cpp
#include <google/protobuf/struct.pb.h>
#include <iostream>

using google::protobuf::Value;

// Helper function to print Value based on its type
void print_value(const std::string& key, const Value& value) {
    std::cout << key << ": ";
    
    switch (value.kind_case()) {
        case Value::kNullValue:
            std::cout << "null";
            break;
        case Value::kNumberValue:
            std::cout << value.number_value();
            break;
        case Value::kStringValue:
            std::cout << "\"" << value.string_value() << "\"";
            break;
        case Value::kBoolValue:
            std::cout << (value.bool_value() ? "true" : "false");
            break;
        case Value::kStructValue:
            std::cout << "{...}";
            break;
        case Value::kListValue:
            std::cout << "[" << value.list_value().values_size() << " items]";
            break;
        default:
            std::cout << "unknown";
    }
    std::cout << std::endl;
}

// Create various Value types
void demonstrate_value_types() {
    Value str_val, num_val, bool_val, null_val, list_val, struct_val;
    
    str_val.set_string_value("Hello, World!");
    num_val.set_number_value(42.5);
    bool_val.set_bool_value(true);
    null_val.set_null_value(NULL_VALUE);
    
    // Create a list value
    auto* list = list_val.mutable_list_value();
    list->add_values()->set_number_value(1);
    list->add_values()->set_number_value(2);
    list->add_values()->set_number_value(3);
    
    // Create a struct value
    auto* struct_data = struct_val.mutable_struct_value();
    (*struct_data->mutable_fields())["key"].set_string_value("value");
    
    print_value("String", str_val);
    print_value("Number", num_val);
    print_value("Boolean", bool_val);
    print_value("Null", null_val);
    print_value("List", list_val);
    print_value("Struct", struct_val);
}
```

## Rust Implementation

### Adding Dependencies

First, add to your `Cargo.toml`:

```toml
[dependencies]
prost = "0.12"
prost-types = "0.12"
serde_json = "1.0"
```

### Building and Accessing Struct Data

```rust
use prost_types::{Struct, Value, ListValue, value::Kind};
use std::collections::HashMap;

/// Create a Struct programmatically
fn create_user_metadata() -> Struct {
    let mut fields = HashMap::new();
    
    // Add string value
    fields.insert(
        "name".to_string(),
        Value {
            kind: Some(Kind::StringValue("John Doe".to_string())),
        },
    );
    
    // Add number value
    fields.insert(
        "age".to_string(),
        Value {
            kind: Some(Kind::NumberValue(30.0)),
        },
    );
    
    // Add boolean value
    fields.insert(
        "is_active".to_string(),
        Value {
            kind: Some(Kind::BoolValue(true)),
        },
    );
    
    // Add null value
    fields.insert(
        "avatar".to_string(),
        Value {
            kind: Some(Kind::NullValue(0)),
        },
    );
    
    // Add nested struct
    let mut address_fields = HashMap::new();
    address_fields.insert(
        "street".to_string(),
        Value {
            kind: Some(Kind::StringValue("123 Main St".to_string())),
        },
    );
    address_fields.insert(
        "city".to_string(),
        Value {
            kind: Some(Kind::StringValue("Springfield".to_string())),
        },
    );
    address_fields.insert(
        "zip".to_string(),
        Value {
            kind: Some(Kind::NumberValue(12345.0)),
        },
    );
    
    fields.insert(
        "address".to_string(),
        Value {
            kind: Some(Kind::StructValue(Struct {
                fields: address_fields,
            })),
        },
    );
    
    // Add array/list
    let hobbies = vec![
        Value {
            kind: Some(Kind::StringValue("reading".to_string())),
        },
        Value {
            kind: Some(Kind::StringValue("coding".to_string())),
        },
        Value {
            kind: Some(Kind::StringValue("hiking".to_string())),
        },
    ];
    
    fields.insert(
        "hobbies".to_string(),
        Value {
            kind: Some(Kind::ListValue(ListValue { values: hobbies })),
        },
    );
    
    Struct { fields }
}

/// Read and print Struct data
fn read_struct_data(metadata: &Struct) {
    // Access string value
    if let Some(Value { kind: Some(Kind::StringValue(name)) }) = metadata.fields.get("name") {
        println!("Name: {}", name);
    }
    
    // Access number value
    if let Some(Value { kind: Some(Kind::NumberValue(age)) }) = metadata.fields.get("age") {
        println!("Age: {}", age);
    }
    
    // Access boolean value
    if let Some(Value { kind: Some(Kind::BoolValue(active)) }) = metadata.fields.get("is_active") {
        println!("Active: {}", if *active { "yes" } else { "no" });
    }
    
    // Check for null
    if let Some(Value { kind: Some(Kind::NullValue(_)) }) = metadata.fields.get("avatar") {
        println!("Avatar: null");
    }
    
    // Access nested struct
    if let Some(Value { kind: Some(Kind::StructValue(addr)) }) = metadata.fields.get("address") {
        if let (
            Some(Value { kind: Some(Kind::StringValue(street)) }),
            Some(Value { kind: Some(Kind::StringValue(city)) })
        ) = (addr.fields.get("street"), addr.fields.get("city")) {
            println!("Address: {}, {}", street, city);
        }
    }
    
    // Access list
    if let Some(Value { kind: Some(Kind::ListValue(hobbies)) }) = metadata.fields.get("hobbies") {
        print!("Hobbies: ");
        for hobby in &hobbies.values {
            if let Some(Kind::StringValue(h)) = &hobby.kind {
                print!("{} ", h);
            }
        }
        println!();
    }
}

/// Helper function to print a Value based on its type
fn print_value(key: &str, value: &Value) {
    print!("{}: ", key);
    
    match &value.kind {
        Some(Kind::NullValue(_)) => println!("null"),
        Some(Kind::NumberValue(n)) => println!("{}", n),
        Some(Kind::StringValue(s)) => println!("\"{}\"", s),
        Some(Kind::BoolValue(b)) => println!("{}", b),
        Some(Kind::StructValue(_)) => println!("{{...}}"),
        Some(Kind::ListValue(list)) => println!("[{} items]", list.values.len()),
        None => println!("unknown"),
    }
}

/// Convert JSON to Struct using serde_json
fn json_to_struct(json_str: &str) -> Result<Struct, serde_json::Error> {
    let json_value: serde_json::Value = serde_json::from_str(json_str)?;
    Ok(json_value_to_struct(&json_value))
}

/// Convert serde_json::Value to prost_types::Value
fn json_value_to_value(json: &serde_json::Value) -> Value {
    match json {
        serde_json::Value::Null => Value {
            kind: Some(Kind::NullValue(0)),
        },
        serde_json::Value::Bool(b) => Value {
            kind: Some(Kind::BoolValue(*b)),
        },
        serde_json::Value::Number(n) => Value {
            kind: Some(Kind::NumberValue(n.as_f64().unwrap_or(0.0))),
        },
        serde_json::Value::String(s) => Value {
            kind: Some(Kind::StringValue(s.clone())),
        },
        serde_json::Value::Array(arr) => {
            let values: Vec<Value> = arr.iter().map(json_value_to_value).collect();
            Value {
                kind: Some(Kind::ListValue(ListValue { values })),
            }
        }
        serde_json::Value::Object(_) => Value {
            kind: Some(Kind::StructValue(json_value_to_struct(json))),
        },
    }
}

/// Convert serde_json::Value to prost_types::Struct
fn json_value_to_struct(json: &serde_json::Value) -> Struct {
    let mut fields = HashMap::new();
    
    if let serde_json::Value::Object(obj) = json {
        for (key, value) in obj {
            fields.insert(key.clone(), json_value_to_value(value));
        }
    }
    
    Struct { fields }
}

/// Convert Struct to JSON string
fn struct_to_json(struct_data: &Struct) -> String {
    let json_value = struct_to_json_value(struct_data);
    serde_json::to_string_pretty(&json_value).unwrap_or_default()
}

/// Convert prost_types::Struct to serde_json::Value
fn struct_to_json_value(struct_data: &Struct) -> serde_json::Value {
    let mut map = serde_json::Map::new();
    
    for (key, value) in &struct_data.fields {
        map.insert(key.clone(), value_to_json_value(value));
    }
    
    serde_json::Value::Object(map)
}

/// Convert prost_types::Value to serde_json::Value
fn value_to_json_value(value: &Value) -> serde_json::Value {
    match &value.kind {
        Some(Kind::NullValue(_)) => serde_json::Value::Null,
        Some(Kind::BoolValue(b)) => serde_json::Value::Bool(*b),
        Some(Kind::NumberValue(n)) => {
            serde_json::Number::from_f64(*n)
                .map(serde_json::Value::Number)
                .unwrap_or(serde_json::Value::Null)
        }
        Some(Kind::StringValue(s)) => serde_json::Value::String(s.clone()),
        Some(Kind::ListValue(list)) => {
            let arr: Vec<serde_json::Value> = list
                .values
                .iter()
                .map(value_to_json_value)
                .collect();
            serde_json::Value::Array(arr)
        }
        Some(Kind::StructValue(s)) => struct_to_json_value(s),
        None => serde_json::Value::Null,
    }
}

fn main() {
    // Create and populate struct
    let metadata = create_user_metadata();
    
    // Read the data
    println!("=== Reading Struct Data ===");
    read_struct_data(&metadata);
    
    // Convert to JSON
    println!("\n=== Struct as JSON ===");
    println!("{}", struct_to_json(&metadata));
    
    // Parse JSON into Struct
    println!("\n=== Parsing JSON to Struct ===");
    let json_input = r#"{
        "username": "alice",
        "score": 95.5,
        "verified": true,
        "settings": {
            "theme": "dark",
            "notifications": true
        }
    }"#;
    
    match json_to_struct(json_input) {
        Ok(parsed_struct) => read_struct_data(&parsed_struct),
        Err(e) => eprintln!("Failed to parse JSON: {}", e),
    }
}
```

### Working with Value Type Directly in Rust

```rust
use prost_types::{Value, value::Kind};

fn demonstrate_value_types() {
    // Create various Value types
    let str_val = Value {
        kind: Some(Kind::StringValue("Hello, World!".to_string())),
    };
    
    let num_val = Value {
        kind: Some(Kind::NumberValue(42.5)),
    };
    
    let bool_val = Value {
        kind: Some(Kind::BoolValue(true)),
    };
    
    let null_val = Value {
        kind: Some(Kind::NullValue(0)),
    };
    
    print_value("String", &str_val);
    print_value("Number", &num_val);
    print_value("Boolean", &bool_val);
    print_value("Null", &null_val);
}
```

## Summary

**Struct and Value types** in Protocol Buffers provide a powerful mechanism for handling dynamic, JSON-like data within the strongly-typed Protobuf ecosystem. These well-known types bridge the gap between schema-based and schemaless data representations.

**Key Takeaways:**

- **Struct** represents JSON objects as key-value maps
- **Value** is a polymorphic type that can hold any JSON value (null, number, string, boolean, struct, or list)
- **ListValue** represents JSON arrays
- **NullValue** explicitly represents JSON null

**When to Use:**
- APIs accepting flexible or varying input structures
- Configuration systems with dynamic schemas
- Interoperability between JSON and Protobuf systems
- User-defined metadata or extensions
- Gradual migration from JSON to strongly-typed Protobuf

**Trade-offs:**
- **Pros**: Maximum flexibility, seamless JSON conversion, no schema updates needed
- **Cons**: Loss of compile-time type safety, larger message sizes, potential runtime errors, reduced performance compared to strongly-typed messages

Both C++ and Rust implementations provide intuitive APIs for creating, reading, and converting between Struct/Value types and JSON, making it straightforward to integrate dynamic data handling into your Protobuf-based applications.