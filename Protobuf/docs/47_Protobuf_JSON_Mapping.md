# Protobuf JSON Mapping

## Overview

Protocol Buffers (Protobuf) provides a canonical JSON encoding that allows seamless interoperability between systems that use JSON and those that use binary Protobuf encoding. This mapping is particularly valuable for web applications, RESTful APIs, and systems that need human-readable serialization formats while maintaining compatibility with Protobuf's efficient binary encoding.

## Key Concepts

**Canonical JSON Encoding** refers to the standardized way Protobuf messages are converted to and from JSON format. This ensures consistency across different implementations and languages, making it possible to exchange JSON data reliably between systems using different Protobuf libraries.

The JSON mapping handles several important aspects:

- Bidirectional conversion between Protobuf binary format and JSON
- Special handling for well-known types (Timestamp, Duration, Any, etc.)
- Preservation of field names and types
- Default value handling
- Enum representation
- Map and repeated field encoding

## Field Name Mapping

By default, Protobuf uses `lowerCamelCase` for JSON field names, converting `snake_case` proto field names. For example, `user_name` in proto becomes `userName` in JSON. You can customize this behavior using the `json_name` field option.

```protobuf
message User {
  string user_name = 1;  // JSON: "userName"
  string email_address = 2 [json_name = "email"];  // JSON: "email"
}
```

## Type Mappings

Different Protobuf types map to JSON types in specific ways:

- **Scalar types**: int32, int64, uint32, uint64, float, double map to JSON numbers (int64/uint64 as strings by default)
- **bool**: Maps to JSON true/false
- **string**: Maps to JSON string
- **bytes**: Base64-encoded string
- **enums**: String enum name or integer value
- **messages**: JSON objects
- **repeated fields**: JSON arrays
- **maps**: JSON objects

## C/C++ Examples

### Basic JSON Serialization

```c
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <string>

// Assuming you have a proto definition:
// message Person {
//   string name = 1;
//   int32 age = 2;
//   repeated string hobbies = 3;
// }

int main() {
    Person person;
    person.set_name("Alice");
    person.set_age(30);
    person.add_hobbies("reading");
    person.add_hobbies("hiking");

    // Convert Protobuf message to JSON
    std::string json_output;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.always_print_primitive_fields = true;
    options.preserve_proto_field_names = false;  // Use lowerCamelCase
    
    google::protobuf::util::Status status = 
        google::protobuf::util::MessageToJsonString(person, &json_output, options);
    
    if (status.ok()) {
        std::cout << "JSON output:\n" << json_output << std::endl;
    } else {
        std::cerr << "Serialization failed: " << status.ToString() << std::endl;
    }

    return 0;
}
```

### JSON Deserialization

```c
#include <google/protobuf/util/json_util.h>

int main() {
    std::string json_input = R"({
        "name": "Bob",
        "age": 25,
        "hobbies": ["coding", "gaming"]
    })";

    Person person;
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = false;  // Strict parsing
    
    google::protobuf::util::Status status = 
        google::protobuf::util::JsonStringToMessage(json_input, &person, options);
    
    if (status.ok()) {
        std::cout << "Name: " << person.name() << std::endl;
        std::cout << "Age: " << person.age() << std::endl;
        for (const auto& hobby : person.hobbies()) {
            std::cout << "Hobby: " << hobby << std::endl;
        }
    } else {
        std::cerr << "Deserialization failed: " << status.ToString() << std::endl;
    }

    return 0;
}
```

### Handling Well-Known Types

```c
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <google/protobuf/util/json_util.h>

// Proto definition:
// message Event {
//   google.protobuf.Timestamp event_time = 1;
//   google.protobuf.Duration duration = 2;
//   google.protobuf.Int32Value optional_count = 3;
// }

int main() {
    Event event;
    
    // Set timestamp (current time)
    auto* timestamp = event.mutable_event_time();
    timestamp->set_seconds(time(nullptr));
    timestamp->set_nanos(0);
    
    // Set duration (2 hours, 30 minutes)
    auto* duration = event.mutable_duration();
    duration->set_seconds(9000);  // 2.5 hours
    
    // Set wrapped value
    event.mutable_optional_count()->set_value(42);
    
    // Convert to JSON
    std::string json_output;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    
    google::protobuf::util::MessageToJsonString(event, &json_output, options);
    
    // JSON output will be:
    // {
    //   "eventTime": "2025-01-18T12:00:00Z",
    //   "duration": "9000s",
    //   "optionalCount": 42
    // }
    
    std::cout << json_output << std::endl;
    
    return 0;
}
```

### Custom JSON Options

```c
#include <google/protobuf/util/json_util.h>

void demonstrateJsonOptions() {
    Person person;
    person.set_name("Charlie");
    person.set_age(0);  // Default value
    
    std::string json_output;
    
    // Option 1: Compact output, skip default values
    google::protobuf::util::JsonPrintOptions compact;
    compact.add_whitespace = false;
    compact.always_print_primitive_fields = false;
    google::protobuf::util::MessageToJsonString(person, &json_output, compact);
    // Output: {"name":"Charlie"}
    
    // Option 2: Pretty print, include defaults
    google::protobuf::util::JsonPrintOptions verbose;
    verbose.add_whitespace = true;
    verbose.always_print_primitive_fields = true;
    google::protobuf::util::MessageToJsonString(person, &json_output, verbose);
    // Output:
    // {
    //   "name": "Charlie",
    //   "age": 0,
    //   "hobbies": []
    // }
    
    // Option 3: Preserve proto field names (snake_case)
    google::protobuf::util::JsonPrintOptions preserve;
    preserve.preserve_proto_field_names = true;
    // Would use "user_name" instead of "userName" in JSON
}
```

## Rust Examples

### Basic JSON Serialization with prost

```rust
use prost::Message;
use serde::{Deserialize, Serialize};

// Proto definition would be:
// message Person {
//   string name = 1;
//   int32 age = 2;
//   repeated string hobbies = 3;
// }

#[derive(Clone, PartialEq, Message, Serialize, Deserialize)]
pub struct Person {
    #[prost(string, tag = "1")]
    pub name: String,
    
    #[prost(int32, tag = "2")]
    pub age: i32,
    
    #[prost(string, repeated, tag = "3")]
    pub hobbies: Vec<String>,
}

fn main() {
    let person = Person {
        name: "Alice".to_string(),
        age: 30,
        hobbies: vec!["reading".to_string(), "hiking".to_string()],
    };
    
    // Serialize to JSON using serde_json
    match serde_json::to_string_pretty(&person) {
        Ok(json) => println!("JSON output:\n{}", json),
        Err(e) => eprintln!("Serialization failed: {}", e),
    }
    
    // Compact JSON
    let compact_json = serde_json::to_string(&person).unwrap();
    println!("\nCompact: {}", compact_json);
}
```

### JSON Deserialization

```rust
use serde_json;

fn deserialize_example() {
    let json_input = r#"{
        "name": "Bob",
        "age": 25,
        "hobbies": ["coding", "gaming"]
    }"#;
    
    match serde_json::from_str::<Person>(json_input) {
        Ok(person) => {
            println!("Name: {}", person.name);
            println!("Age: {}", person.age);
            for hobby in &person.hobbies {
                println!("Hobby: {}", hobby);
            }
        }
        Err(e) => eprintln!("Deserialization failed: {}", e),
    }
}
```

### Handling Well-Known Types

```rust
use prost::Message;
use prost_types::{Timestamp, Duration};
use serde::{Deserialize, Serialize};

#[derive(Clone, PartialEq, Message, Serialize, Deserialize)]
pub struct Event {
    #[prost(message, optional, tag = "1")]
    #[serde(with = "timestamp_serde")]
    pub event_time: Option<Timestamp>,
    
    #[prost(message, optional, tag = "2")]
    #[serde(with = "duration_serde")]
    pub duration: Option<Duration>,
    
    #[prost(int32, optional, tag = "3")]
    pub optional_count: Option<i32>,
}

// Custom serde module for Timestamp
mod timestamp_serde {
    use prost_types::Timestamp;
    use serde::{Deserialize, Deserializer, Serializer};
    
    pub fn serialize<S>(timestamp: &Option<Timestamp>, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match timestamp {
            Some(ts) => {
                let datetime = chrono::NaiveDateTime::from_timestamp_opt(
                    ts.seconds, 
                    ts.nanos as u32
                ).unwrap();
                serializer.serialize_str(&datetime.format("%Y-%m-%dT%H:%M:%SZ").to_string())
            }
            None => serializer.serialize_none(),
        }
    }
    
    pub fn deserialize<'de, D>(deserializer: D) -> Result<Option<Timestamp>, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s: Option<String> = Option::deserialize(deserializer)?;
        match s {
            Some(s) => {
                // Parse ISO 8601 timestamp
                // Implementation would parse the string and create Timestamp
                Ok(Some(Timestamp { seconds: 0, nanos: 0 }))
            }
            None => Ok(None),
        }
    }
}

// Similar module for Duration
mod duration_serde {
    use prost_types::Duration;
    use serde::{Deserializer, Serializer};
    
    pub fn serialize<S>(duration: &Option<Duration>, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match duration {
            Some(d) => serializer.serialize_str(&format!("{}s", d.seconds)),
            None => serializer.serialize_none(),
        }
    }
    
    pub fn deserialize<'de, D>(deserializer: D) -> Result<Option<Duration>, D::Error>
    where
        D: Deserializer<'de>,
    {
        // Parse duration string like "9000s"
        Ok(Some(Duration { seconds: 9000, nanos: 0 }))
    }
}
```

### Enum Handling

```rust
use prost::Message;
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(i32)]
pub enum Status {
    Unknown = 0,
    Active = 1,
    Inactive = 2,
    Suspended = 3,
}

impl prost::Enumeration for Status {
    fn from_i32(value: i32) -> Option<Self> {
        match value {
            0 => Some(Status::Unknown),
            1 => Some(Status::Active),
            2 => Some(Status::Inactive),
            3 => Some(Status::Suspended),
            _ => None,
        }
    }
    
    fn as_i32(&self) -> i32 {
        *self as i32
    }
}

// Custom serialization to use string representation
impl Serialize for Status {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let s = match self {
            Status::Unknown => "UNKNOWN",
            Status::Active => "ACTIVE",
            Status::Inactive => "INACTIVE",
            Status::Suspended => "SUSPENDED",
        };
        serializer.serialize_str(s)
    }
}

impl<'de> Deserialize<'de> for Status {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        match s.as_str() {
            "UNKNOWN" => Ok(Status::Unknown),
            "ACTIVE" => Ok(Status::Active),
            "INACTIVE" => Ok(Status::Inactive),
            "SUSPENDED" => Ok(Status::Suspended),
            _ => Err(serde::de::Error::custom("invalid status")),
        }
    }
}

#[derive(Clone, PartialEq, Message, Serialize, Deserialize)]
pub struct Account {
    #[prost(string, tag = "1")]
    pub username: String,
    
    #[prost(enumeration = "Status", tag = "2")]
    pub status: i32,
}
```

### Map Fields

```rust
use prost::Message;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, PartialEq, Message, Serialize, Deserialize)]
pub struct Configuration {
    #[prost(map = "string, string", tag = "1")]
    pub settings: HashMap<String, String>,
    
    #[prost(map = "string, int32", tag = "2")]
    pub counters: HashMap<String, i32>,
}

fn map_example() {
    let mut config = Configuration {
        settings: HashMap::new(),
        counters: HashMap::new(),
    };
    
    config.settings.insert("theme".to_string(), "dark".to_string());
    config.settings.insert("language".to_string(), "en".to_string());
    config.counters.insert("login_attempts".to_string(), 3);
    
    // Serialize to JSON
    let json = serde_json::to_string_pretty(&config).unwrap();
    println!("{}", json);
    
    // JSON output:
    // {
    //   "settings": {
    //     "theme": "dark",
    //     "language": "en"
    //   },
    //   "counters": {
    //     "login_attempts": 3
    //   }
    // }
}
```

## Special Types Handling

### Timestamp

**JSON format**: RFC 3339 string (e.g., "2025-01-18T12:00:00Z")

**C++ example**:
```c
google::protobuf::Timestamp timestamp;
timestamp.set_seconds(1705579200);
// JSON: "2025-01-18T12:00:00Z"
```

### Duration

**JSON format**: String with 's' suffix (e.g., "3.5s")

**C++ example**:
```c
google::protobuf::Duration duration;
duration.set_seconds(3);
duration.set_nanos(500000000);
// JSON: "3.500000000s"
```

### Any

The `Any` type allows embedding arbitrary message types. In JSON, it's represented as an object with a `@type` field.

**JSON format**:
```json
{
  "@type": "type.googleapis.com/packagename.MessageType",
  "field1": "value1",
  "field2": "value2"
}
```

### FieldMask

**JSON format**: Comma-separated list of field paths (e.g., "user.name,user.email")

### Struct, Value, ListValue

These map directly to JSON objects, values, and arrays respectively, enabling dynamic JSON structures.

## Interoperability Considerations

### Case Sensitivity

JSON field names are case-sensitive. The canonical mapping uses `lowerCamelCase`, but you must ensure consistency across your system.

### Unknown Fields

When parsing JSON, you can choose to ignore unknown fields or treat them as errors. This affects forward/backward compatibility.

**C++ approach**:
```c
google::protobuf::util::JsonParseOptions options;
options.ignore_unknown_fields = true;  // For flexibility
```

**Rust approach**: Serde typically ignores unknown fields by default, but you can make it strict with `#[serde(deny_unknown_fields)]`.

### Default Values

Protobuf distinguishes between unset fields and fields set to default values, but JSON doesn't. When serializing to JSON, you can choose whether to include fields with default values.

### 64-bit Integers

JavaScript cannot precisely represent integers larger than 2^53. By default, int64 and uint64 are serialized as strings in JSON to preserve precision.

**C++ configuration**:
```c
google::protobuf::util::JsonPrintOptions options;
options.always_print_primitive_fields = true;
options.preserve_proto_field_names = false;
```

### Binary Data

Bytes fields are encoded as base64 strings in JSON, which increases size by approximately 33%.

### Enum Values

Enums can be represented as either strings (enum name) or integers. String representation is more readable and resilient to enum reordering.

### Null Values

In proto3, setting a field to its default value makes it unset. JSON null is mapped to the default value of the field type.

## Performance Considerations

JSON serialization/deserialization is generally slower and produces larger output than binary Protobuf:

- **Size**: JSON is typically 2-10x larger than binary Protobuf
- **Speed**: Binary encoding/decoding is 3-10x faster
- **Human readability**: JSON wins for debugging and interoperability

Use JSON when:
- Interfacing with web browsers or JavaScript applications
- Debugging or logging
- Integrating with systems that only support JSON
- Human readability is important

Use binary Protobuf when:
- Performance is critical
- Bandwidth is limited
- Communicating between backend services

## Summary

Protobuf's JSON mapping provides a standardized, canonical way to convert between efficient binary Protobuf encoding and human-readable JSON format. The mapping handles all Protobuf types including scalar values, messages, enums, repeated fields, maps, and well-known types like Timestamp and Duration. Both C/C++ (via google::protobuf::util) and Rust (via prost with serde) provide robust support for JSON serialization with configurable options for field naming, default value handling, and formatting.

Key interoperability considerations include handling of 64-bit integers as strings to preserve precision in JavaScript, base64 encoding for binary data, case-sensitive field names using lowerCamelCase convention, and proper handling of unknown fields for forward compatibility. While JSON encoding is less efficient than binary Protobuf in terms of both size and speed, it offers crucial advantages for web APIs, debugging, and integration with JSON-based systems. The choice between JSON and binary encoding should be based on specific requirements for readability, performance, and interoperability with the target environment.