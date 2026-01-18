# Message Structure and Composition in Protocol Buffers

## Overview

Protocol Buffers (Protobuf) provides a powerful system for defining structured data through messages. Understanding how to properly compose messages—including field definitions, nested structures, and composition patterns—is essential for building maintainable and efficient data models.

## Core Concepts

### Message Basics

A Protobuf message is a logical grouping of typed fields. Each field has:
- A **type** (scalar, enum, or another message)
- A **name** (identifier)
- A **field number** (unique within the message, used for binary encoding)
- A **field rule** (singular, repeated, optional, or map)

### Field Numbers

Field numbers are critical—they identify fields in the binary format and must remain stable across versions. Numbers 1-15 use 1 byte for encoding, while 16-2047 use 2 bytes, so frequently used fields should have lower numbers.

## Defining Messages with Fields

### Basic Message Definition

```protobuf
syntax = "proto3";

message User {
  string username = 1;
  string email = 2;
  int32 age = 3;
  bool is_active = 4;
  repeated string tags = 5;
}
```

### Field Types

**Scalar types**: `double`, `float`, `int32`, `int64`, `uint32`, `uint64`, `sint32`, `sint64`, `fixed32`, `fixed64`, `sfixed32`, `sfixed64`, `bool`, `string`, `bytes`

**Repeated fields**: Collections of values (like arrays/lists)

**Maps**: Key-value pairs with syntax `map<key_type, value_type> field_name = N;`

## Nested Messages

Nested messages allow hierarchical data structures and better organization.

### Simple Nesting

```protobuf
message Organization {
  string name = 1;
  
  message Address {
    string street = 1;
    string city = 2;
    string postal_code = 3;
    string country = 4;
  }
  
  Address headquarters = 2;
  repeated Address branches = 3;
}
```

### Deep Nesting

```protobuf
message Company {
  string company_name = 1;
  
  message Department {
    string dept_name = 1;
    
    message Employee {
      string name = 1;
      string employee_id = 2;
      string role = 3;
    }
    
    Employee manager = 2;
    repeated Employee staff = 3;
  }
  
  repeated Department departments = 2;
}
```

## Composition Patterns

### 1. **Composition over Inheritance**

Protobuf doesn't support inheritance, so use composition:

```protobuf
message Timestamp {
  int64 seconds = 1;
  int32 nanos = 2;
}

message AuditInfo {
  Timestamp created_at = 1;
  Timestamp updated_at = 2;
  string created_by = 3;
  string updated_by = 4;
}

message Document {
  string doc_id = 1;
  string title = 2;
  string content = 3;
  AuditInfo audit = 4;  // Composition
}

message BlogPost {
  string post_id = 1;
  string title = 2;
  string body = 3;
  AuditInfo audit = 4;  // Reuse same audit info
}
```

### 2. **Oneof for Polymorphism**

Use `oneof` when a message can contain one of several field types:

```protobuf
message SearchResult {
  oneof result {
    ImageResult image = 1;
    VideoResult video = 2;
    TextResult text = 3;
  }
}

message ImageResult {
  string url = 1;
  int32 width = 2;
  int32 height = 3;
}

message VideoResult {
  string url = 1;
  int32 duration_seconds = 2;
}

message TextResult {
  string snippet = 1;
  string source = 2;
}
```

### 3. **Optional Fields (Proto3)**

```protobuf
message UserProfile {
  string user_id = 1;
  string username = 2;
  optional string bio = 3;         // Explicitly optional
  optional int32 age = 4;           // Can distinguish between 0 and unset
  optional Timestamp last_login = 5;
}
```

## Code Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "organization.pb-c.h"

int main() {
    // Create an address
    Organization__Address address = ORGANIZATION__ADDRESS__INIT;
    address.street = "123 Tech Street";
    address.city = "San Francisco";
    address.postal_code = "94105";
    address.country = "USA";
    
    // Create organization with nested address
    Organization org = ORGANIZATION__INIT;
    org.name = "TechCorp Inc.";
    org.headquarters = &address;
    
    // Add branch offices
    Organization__Address branch1 = ORGANIZATION__ADDRESS__INIT;
    branch1.street = "456 Innovation Ave";
    branch1.city = "New York";
    branch1.postal_code = "10001";
    branch1.country = "USA";
    
    Organization__Address *branches[] = {&branch1};
    org.branches = branches;
    org.n_branches = 1;
    
    // Serialize
    size_t len = organization__get_packed_size(&org);
    uint8_t *buffer = malloc(len);
    organization__pack(&org, buffer);
    
    printf("Serialized %zu bytes\n", len);
    printf("Organization: %s\n", org.name);
    printf("HQ: %s, %s\n", org.headquarters->city, org.headquarters->country);
    
    // Deserialize
    Organization *deserialized = organization__unpack(NULL, len, buffer);
    if (deserialized) {
        printf("Deserialized organization: %s\n", deserialized->name);
        printf("Branches: %zu\n", deserialized->n_branches);
        organization__free_unpacked(deserialized, NULL);
    }
    
    free(buffer);
    return 0;
}
```

### C++ Implementation

```cpp
#include <iostream>
#include <fstream>
#include <string>
#include "company.pb.h"

using namespace std;

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    // Create a company with nested departments and employees
    Company company;
    company.set_company_name("InnovateTech");
    
    // Add Engineering department
    Company::Department* eng_dept = company.add_departments();
    eng_dept->set_dept_name("Engineering");
    
    // Add manager to Engineering
    Company::Department::Employee* manager = eng_dept->mutable_manager();
    manager->set_name("Alice Johnson");
    manager->set_employee_id("EMP001");
    manager->set_role("Engineering Manager");
    
    // Add staff members
    Company::Department::Employee* staff1 = eng_dept->add_staff();
    staff1->set_name("Bob Smith");
    staff1->set_employee_id("EMP002");
    staff1->set_role("Senior Engineer");
    
    Company::Department::Employee* staff2 = eng_dept->add_staff();
    staff2->set_name("Carol White");
    staff2->set_employee_id("EMP003");
    staff2->set_role("Software Engineer");
    
    // Add Marketing department
    Company::Department* mkt_dept = company.add_departments();
    mkt_dept->set_dept_name("Marketing");
    
    Company::Department::Employee* mkt_manager = mkt_dept->mutable_manager();
    mkt_manager->set_name("David Brown");
    mkt_manager->set_employee_id("EMP004");
    mkt_manager->set_role("Marketing Director");
    
    // Serialize to file
    fstream output("company.bin", ios::out | ios::binary | ios::trunc);
    if (!company.SerializeToOstream(&output)) {
        cerr << "Failed to serialize company data." << endl;
        return -1;
    }
    output.close();
    
    // Deserialize from file
    Company loaded_company;
    fstream input("company.bin", ios::in | ios::binary);
    if (!loaded_company.ParseFromIstream(&input)) {
        cerr << "Failed to parse company data." << endl;
        return -1;
    }
    input.close();
    
    // Display loaded data
    cout << "Company: " << loaded_company.company_name() << endl;
    cout << "Departments: " << loaded_company.departments_size() << endl;
    
    for (const auto& dept : loaded_company.departments()) {
        cout << "\nDepartment: " << dept.dept_name() << endl;
        cout << "  Manager: " << dept.manager().name() 
             << " (" << dept.manager().role() << ")" << endl;
        cout << "  Staff count: " << dept.staff_size() << endl;
        
        for (const auto& employee : dept.staff()) {
            cout << "    - " << employee.name() 
                 << " [" << employee.employee_id() << "] - " 
                 << employee.role() << endl;
        }
    }
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

### Rust Implementation

```rust
// Assuming generated code from prost or similar

use std::fs::File;
use std::io::{Read, Write};

// Proto definition would be:
// message Document {
//   string doc_id = 1;
//   string title = 2;
//   string content = 3;
//   AuditInfo audit = 4;
// }
//
// message AuditInfo {
//   Timestamp created_at = 1;
//   Timestamp updated_at = 2;
//   string created_by = 3;
//   string updated_by = 4;
// }

mod proto {
    include!(concat!(env!("OUT_DIR"), "/proto.rs"));
}

use proto::{Document, AuditInfo, Timestamp};
use prost::Message;

fn create_timestamp(seconds: i64, nanos: i32) -> Timestamp {
    Timestamp { seconds, nanos }
}

fn create_audit_info(creator: &str) -> AuditInfo {
    let now = create_timestamp(1704067200, 0); // Example timestamp
    
    AuditInfo {
        created_at: Some(now.clone()),
        updated_at: Some(now),
        created_by: creator.to_string(),
        updated_by: creator.to_string(),
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create a document with nested audit info
    let document = Document {
        doc_id: "DOC-12345".to_string(),
        title: "Protobuf Guide".to_string(),
        content: "A comprehensive guide to Protocol Buffers...".to_string(),
        audit: Some(create_audit_info("john.doe@example.com")),
    };
    
    // Serialize to bytes
    let mut buffer = Vec::new();
    document.encode(&mut buffer)?;
    println!("Serialized {} bytes", buffer.len());
    
    // Write to file
    let mut file = File::create("document.bin")?;
    file.write_all(&buffer)?;
    
    // Read from file
    let mut read_buffer = Vec::new();
    let mut read_file = File::open("document.bin")?;
    read_file.read_to_end(&mut read_buffer)?;
    
    // Deserialize
    let loaded_doc = Document::decode(&read_buffer[..])?;
    
    println!("\nLoaded Document:");
    println!("  ID: {}", loaded_doc.doc_id);
    println!("  Title: {}", loaded_doc.title);
    println!("  Content length: {} chars", loaded_doc.content.len());
    
    if let Some(audit) = loaded_doc.audit {
        println!("\nAudit Information:");
        println!("  Created by: {}", audit.created_by);
        println!("  Updated by: {}", audit.updated_by);
        
        if let Some(created) = audit.created_at {
            println!("  Created at: {} seconds", created.seconds);
        }
    }
    
    Ok(())
}
```

### Rust with Oneof Example

```rust
mod proto {
    // Generated from:
    // message SearchResult {
    //   oneof result {
    //     ImageResult image = 1;
    //     VideoResult video = 2;
    //     TextResult text = 3;
    //   }
    // }
    include!(concat!(env!("OUT_DIR"), "/search.rs"));
}

use proto::{SearchResult, ImageResult, VideoResult, TextResult};
use proto::search_result::Result as SearchResultType;
use prost::Message;

fn process_search_result(result: &SearchResult) {
    match &result.result {
        Some(SearchResultType::Image(img)) => {
            println!("Image result: {} ({}x{})", img.url, img.width, img.height);
        }
        Some(SearchResultType::Video(vid)) => {
            println!("Video result: {} (duration: {}s)", 
                     vid.url, vid.duration_seconds);
        }
        Some(SearchResultType::Text(txt)) => {
            println!("Text result from {}: {}", txt.source, txt.snippet);
        }
        None => {
            println!("Empty search result");
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create different types of search results
    let image_result = SearchResult {
        result: Some(SearchResultType::Image(ImageResult {
            url: "https://example.com/image.jpg".to_string(),
            width: 1920,
            height: 1080,
        })),
    };
    
    let video_result = SearchResult {
        result: Some(SearchResultType::Video(VideoResult {
            url: "https://example.com/video.mp4".to_string(),
            duration_seconds: 300,
        })),
    };
    
    let text_result = SearchResult {
        result: Some(SearchResultType::Text(TextResult {
            snippet: "Protocol Buffers are a language-neutral...".to_string(),
            source: "Wikipedia".to_string(),
        })),
    };
    
    // Process results
    process_search_result(&image_result);
    process_search_result(&video_result);
    process_search_result(&text_result);
    
    // Serialize and deserialize
    let mut buffer = Vec::new();
    image_result.encode(&mut buffer)?;
    
    let decoded = SearchResult::decode(&buffer[..])?;
    process_search_result(&decoded);
    
    Ok(())
}
```

## Best Practices

### 1. **Field Numbering Strategy**
- Reserve 1-15 for frequently used fields (more efficient encoding)
- Reserve ranges for future use: `reserved 100 to 199;`
- Never reuse deleted field numbers: `reserved 5, 8 to 10;`

### 2. **Message Granularity**
- Keep messages focused and cohesive
- Extract common patterns into reusable messages
- Avoid overly deep nesting (3-4 levels maximum)

### 3. **Naming Conventions**
- Use PascalCase for message names: `UserProfile`
- Use snake_case for field names: `user_id`, `created_at`
- Use descriptive names that convey purpose

### 4. **Backward Compatibility**
- Never change field numbers
- Never change field types (with rare exceptions)
- Use `optional` when fields might not always be present
- Add new fields instead of modifying existing ones

### 5. **Performance Considerations**
- Group related fields with similar access patterns
- Use appropriate numeric types (`int32` vs `int64`)
- Consider `bytes` for large binary data
- Use `repeated` efficiently (pre-allocate when possible)

## Common Patterns

### Envelope Pattern
```protobuf
message Envelope {
  string message_id = 1;
  string message_type = 2;
  google.protobuf.Any payload = 3;
  map<string, string> metadata = 4;
}
```

### Pagination Pattern
```protobuf
message PageRequest {
  int32 page_size = 1;
  string page_token = 2;
}

message PageResponse {
  repeated Item items = 1;
  string next_page_token = 2;
  int32 total_count = 3;
}
```

### Versioning Pattern
```protobuf
message VersionedMessage {
  int32 version = 1;
  oneof payload {
    MessageV1 v1 = 2;
    MessageV2 v2 = 3;
  }
}
```

## Summary

Protocol Buffers message structure and composition provides a robust framework for defining complex data models. Key takeaways:

- **Messages** are composed of typed fields with unique field numbers that drive binary encoding
- **Nested messages** enable hierarchical structures and better code organization
- **Composition patterns** (embedding messages, oneof, optional fields) provide flexibility without inheritance
- **Field numbers** are permanent identifiers—choose them carefully and never reuse them
- **Best practices** emphasize backward compatibility, appropriate granularity, and thoughtful naming
- Implementation across **C/C++ and Rust** demonstrates consistent patterns despite language differences: creation, serialization, deserialization, and traversal of nested structures

Proper message composition leads to maintainable schemas that evolve gracefully, efficient binary encoding, and clean APIs across multiple programming languages. The examples show how nested structures, composition over inheritance, and oneof fields solve real-world data modeling challenges while maintaining type safety and performance.