# WebSocket Schema Validation: Detailed Overview

## Topic Description

**Schema Validation** in WebSocket programming refers to the process of validating message payloads against predefined schemas to ensure type safety, data integrity, and contract compliance between client and server. This runtime validation catches malformed data, prevents type errors, and enforces communication protocols before messages are processed by application logic.

### Key Concepts

- **Type Safety**: Ensures data conforms to expected types (string, number, boolean, arrays, objects)
- **Structure Validation**: Verifies message structure matches defined schemas
- **Contract Enforcement**: Guarantees both endpoints follow the agreed-upon message format
- **Error Prevention**: Catches invalid data early, preventing runtime errors downstream
- **Documentation**: Schemas serve as living documentation of message formats

### Common Schema Formats

- **JSON Schema**: Industry-standard schema definition language
- **Protocol Buffers**: Google's binary serialization with strong typing
- **MessagePack**: Efficient binary serialization format
- **Custom validators**: Application-specific validation logic

---

## C/C++ Implementation

C/C++ implementations typically use JSON Schema validators or custom validation logic.

### Example 1: Using RapidJSON with Custom Validation

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

typedef websocketpp::server<websocketpp::config::asio> server;

class MessageValidator {
private:
    std::unique_ptr<rapidjson::SchemaDocument> schema;
    
public:
    MessageValidator(const char* schemaJson) {
        rapidjson::Document sd;
        if (sd.Parse(schemaJson).HasParseError()) {
            throw std::runtime_error("Invalid schema JSON");
        }
        schema = std::make_unique<rapidjson::SchemaDocument>(sd);
    }
    
    bool validate(const std::string& message, std::string& error) {
        rapidjson::Document d;
        if (d.Parse(message.c_str()).HasParseError()) {
            error = "Invalid JSON";
            return false;
        }
        
        rapidjson::SchemaValidator validator(*schema);
        if (!d.Accept(validator)) {
            rapidjson::StringBuffer sb;
            validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
            error = "Schema validation failed at: " + std::string(sb.GetString());
            return false;
        }
        
        return true;
    }
};

class WebSocketServer {
private:
    server ws_server;
    MessageValidator validator;
    
    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
        std::string error;
        std::string payload = msg->get_payload();
        
        if (!validator.validate(payload, error)) {
            std::cout << "Validation failed: " << error << std::endl;
            
            // Send error response
            rapidjson::Document response;
            response.SetObject();
            auto& allocator = response.GetAllocator();
            response.AddMember("type", "error", allocator);
            response.AddMember("message", 
                rapidjson::Value(error.c_str(), allocator), allocator);
            
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            response.Accept(writer);
            
            ws_server.send(hdl, buffer.GetString(), 
                websocketpp::frame::opcode::text);
            return;
        }
        
        // Process valid message
        std::cout << "Valid message received: " << payload << std::endl;
        process_message(hdl, payload);
    }
    
    void process_message(websocketpp::connection_hdl hdl, 
                        const std::string& message) {
        // Business logic here
        rapidjson::Document d;
        d.Parse(message.c_str());
        
        std::string type = d["type"].GetString();
        std::cout << "Processing message type: " << type << std::endl;
    }
    
public:
    WebSocketServer() : validator(R"({
        "type": "object",
        "properties": {
            "type": {"type": "string"},
            "payload": {
                "type": "object",
                "properties": {
                    "id": {"type": "integer"},
                    "name": {"type": "string"},
                    "active": {"type": "boolean"}
                },
                "required": ["id", "name"]
            }
        },
        "required": ["type", "payload"]
    })") {
        ws_server.set_message_handler(
            std::bind(&WebSocketServer::on_message, this, 
                std::placeholders::_1, std::placeholders::_2)
        );
    }
    
    void run(uint16_t port) {
        ws_server.init_asio();
        ws_server.listen(port);
        ws_server.start_accept();
        std::cout << "Server running on port " << port << std::endl;
        ws_server.run();
    }
};

int main() {
    try {
        WebSocketServer server;
        server.run(9002);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

### Example 2: Custom Validation with Type Checking

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <cjson/cJSON.h>

typedef enum {
    VALIDATION_SUCCESS,
    VALIDATION_MISSING_FIELD,
    VALIDATION_WRONG_TYPE,
    VALIDATION_INVALID_VALUE
} ValidationResult;

typedef struct {
    const char* field;
    int json_type; // cJSON type
    bool required;
} FieldSchema;

ValidationResult validate_message(const char* json_str, 
                                  FieldSchema* schema, 
                                  int schema_size,
                                  char* error_buffer,
                                  size_t buffer_size) {
    cJSON* json = cJSON_Parse(json_str);
    if (!json) {
        snprintf(error_buffer, buffer_size, "Invalid JSON");
        return VALIDATION_WRONG_TYPE;
    }
    
    for (int i = 0; i < schema_size; i++) {
        cJSON* field = cJSON_GetObjectItem(json, schema[i].field);
        
        if (!field) {
            if (schema[i].required) {
                snprintf(error_buffer, buffer_size, 
                    "Missing required field: %s", schema[i].field);
                cJSON_Delete(json);
                return VALIDATION_MISSING_FIELD;
            }
            continue;
        }
        
        if ((field->type & 0xFF) != schema[i].json_type) {
            snprintf(error_buffer, buffer_size,
                "Wrong type for field: %s", schema[i].field);
            cJSON_Delete(json);
            return VALIDATION_WRONG_TYPE;
        }
    }
    
    cJSON_Delete(json);
    return VALIDATION_SUCCESS;
}

int main() {
    // Define schema
    FieldSchema user_schema[] = {
        {"type", cJSON_String, true},
        {"user_id", cJSON_Number, true},
        {"username", cJSON_String, true},
        {"email", cJSON_String, false},
        {"active", cJSON_True | cJSON_False, true}
    };
    
    // Test messages
    const char* valid_msg = "{\"type\":\"user\",\"user_id\":123,"
        "\"username\":\"john\",\"active\":true}";
    const char* invalid_msg = "{\"type\":\"user\",\"user_id\":\"abc\"}";
    
    char error[256];
    
    // Validate valid message
    ValidationResult result = validate_message(
        valid_msg, user_schema, 5, error, sizeof(error));
    printf("Valid message: %s\n", 
        result == VALIDATION_SUCCESS ? "PASS" : "FAIL");
    
    // Validate invalid message
    result = validate_message(
        invalid_msg, user_schema, 5, error, sizeof(error));
    printf("Invalid message: %s (Error: %s)\n",
        result == VALIDATION_SUCCESS ? "PASS" : "FAIL", error);
    
    return 0;
}
```

---

## Rust Implementation

Rust has excellent support for schema validation through serde and jsonschema crates.

### Example 1: Using jsonschema Crate

```rust
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use jsonschema::{Draft, JSONSchema};
use std::sync::Arc;

#[derive(Debug, Serialize, Deserialize)]
struct UserMessage {
    #[serde(rename = "type")]
    msg_type: String,
    payload: UserPayload,
}

#[derive(Debug, Serialize, Deserialize)]
struct UserPayload {
    id: u32,
    name: String,
    email: Option<String>,
    active: bool,
}

#[derive(Debug, Serialize)]
struct ErrorResponse {
    #[serde(rename = "type")]
    msg_type: String,
    error: String,
}

struct SchemaValidator {
    schema: JSONSchema,
}

impl SchemaValidator {
    fn new(schema_json: Value) -> Result<Self, String> {
        let schema = JSONSchema::options()
            .with_draft(Draft::Draft7)
            .compile(&schema_json)
            .map_err(|e| format!("Schema compilation failed: {}", e))?;
        
        Ok(SchemaValidator { schema })
    }
    
    fn validate(&self, instance: &Value) -> Result<(), Vec<String>> {
        match self.schema.validate(instance) {
            Ok(_) => Ok(()),
            Err(errors) => {
                let error_messages: Vec<String> = errors
                    .map(|e| format!("{} at {}", e, e.instance_path))
                    .collect();
                Err(error_messages)
            }
        }
    }
}

async fn handle_connection(
    stream: tokio::net::TcpStream,
    validator: Arc<SchemaValidator>
) {
    let ws_stream = accept_async(stream)
        .await
        .expect("Error during WebSocket handshake");
    
    let (mut write, mut read) = ws_stream.split();
    
    println!("New WebSocket connection established");
    
    while let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                // Parse JSON
                let json_value: Result<Value, _> = serde_json::from_str(&text);
                
                match json_value {
                    Ok(value) => {
                        // Validate against schema
                        match validator.validate(&value) {
                            Ok(_) => {
                                println!("Valid message: {}", text);
                                
                                // Deserialize to strongly-typed struct
                                match serde_json::from_value::<UserMessage>(value) {
                                    Ok(msg) => {
                                        process_message(msg, &mut write).await;
                                    }
                                    Err(e) => {
                                        send_error(&mut write, 
                                            format!("Deserialization error: {}", e))
                                            .await;
                                    }
                                }
                            }
                            Err(errors) => {
                                let error_msg = errors.join(", ");
                                println!("Validation failed: {}", error_msg);
                                send_error(&mut write, error_msg).await;
                            }
                        }
                    }
                    Err(e) => {
                        send_error(&mut write, 
                            format!("Invalid JSON: {}", e)).await;
                    }
                }
            }
            Ok(Message::Close(_)) => {
                println!("Client disconnected");
                break;
            }
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
            _ => {}
        }
    }
}

async fn process_message(
    msg: UserMessage,
    write: &mut futures_util::stream::SplitSink
        tokio_tungstenite::WebSocketStream<tokio::net::TcpStream>,
        Message
    >
) {
    println!("Processing message type: {}", msg.msg_type);
    println!("User: {} (ID: {})", msg.payload.name, msg.payload.id);
    
    // Send acknowledgment
    let response = json!({
        "type": "ack",
        "user_id": msg.payload.id,
        "status": "processed"
    });
    
    let _ = write.send(Message::Text(response.to_string())).await;
}

async fn send_error(
    write: &mut futures_util::stream::SplitSink
        tokio_tungstenite::WebSocketStream<tokio::net::TcpStream>,
        Message
    >,
    error: String
) {
    let error_response = ErrorResponse {
        msg_type: "error".to_string(),
        error,
    };
    
    if let Ok(json) = serde_json::to_string(&error_response) {
        let _ = write.send(Message::Text(json)).await;
    }
}

#[tokio::main]
async fn main() {
    // Define JSON Schema
    let schema = json!({
        "$schema": "http://json-schema.org/draft-07/schema#",
        "type": "object",
        "properties": {
            "type": {"type": "string"},
            "payload": {
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "minimum": 1},
                    "name": {"type": "string", "minLength": 1},
                    "email": {"type": "string", "format": "email"},
                    "active": {"type": "boolean"}
                },
                "required": ["id", "name", "active"]
            }
        },
        "required": ["type", "payload"]
    });
    
    let validator = Arc::new(
        SchemaValidator::new(schema)
            .expect("Failed to create validator")
    );
    
    let listener = tokio::net::TcpListener::bind("127.0.0.1:9002")
        .await
        .expect("Failed to bind");
    
    println!("WebSocket server listening on ws://127.0.0.1:9002");
    
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        let validator = Arc::clone(&validator);
        tokio::spawn(handle_connection(stream, validator));
    }
}
```

### Example 2: Custom Validation with Builder Pattern

```rust
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug)]
enum ValidationError {
    MissingField(String),
    InvalidType(String),
    InvalidValue(String),
}

trait Validator {
    fn validate(&self, value: &serde_json::Value) -> Result<(), ValidationError>;
}

struct StringValidator {
    min_length: Option<usize>,
    max_length: Option<usize>,
    pattern: Option<regex::Regex>,
}

impl Validator for StringValidator {
    fn validate(&self, value: &serde_json::Value) -> Result<(), ValidationError> {
        let s = value.as_str()
            .ok_or_else(|| ValidationError::InvalidType("Expected string".into()))?;
        
        if let Some(min) = self.min_length {
            if s.len() < min {
                return Err(ValidationError::InvalidValue(
                    format!("String too short (min: {})", min)
                ));
            }
        }
        
        if let Some(max) = self.max_length {
            if s.len() > max {
                return Err(ValidationError::InvalidValue(
                    format!("String too long (max: {})", max)
                ));
            }
        }
        
        if let Some(ref pattern) = self.pattern {
            if !pattern.is_match(s) {
                return Err(ValidationError::InvalidValue(
                    "String doesn't match pattern".into()
                ));
            }
        }
        
        Ok(())
    }
}

struct NumberValidator {
    min: Option<f64>,
    max: Option<f64>,
}

impl Validator for NumberValidator {
    fn validate(&self, value: &serde_json::Value) -> Result<(), ValidationError> {
        let n = value.as_f64()
            .ok_or_else(|| ValidationError::InvalidType("Expected number".into()))?;
        
        if let Some(min) = self.min {
            if n < min {
                return Err(ValidationError::InvalidValue(
                    format!("Number too small (min: {})", min)
                ));
            }
        }
        
        if let Some(max) = self.max {
            if n > max {
                return Err(ValidationError::InvalidValue(
                    format!("Number too large (max: {})", max)
                ));
            }
        }
        
        Ok(())
    }
}

struct ObjectValidator {
    fields: HashMap<String, (Box<dyn Validator + Send + Sync>, bool)>, // (validator, required)
}

impl ObjectValidator {
    fn new() -> Self {
        ObjectValidator {
            fields: HashMap::new(),
        }
    }
    
    fn add_field(
        mut self,
        name: impl Into<String>,
        validator: Box<dyn Validator + Send + Sync>,
        required: bool
    ) -> Self {
        self.fields.insert(name.into(), (validator, required));
        self
    }
}

impl Validator for ObjectValidator {
    fn validate(&self, value: &serde_json::Value) -> Result<(), ValidationError> {
        let obj = value.as_object()
            .ok_or_else(|| ValidationError::InvalidType("Expected object".into()))?;
        
        for (field_name, (validator, required)) in &self.fields {
            match obj.get(field_name) {
                Some(field_value) => validator.validate(field_value)?,
                None if *required => {
                    return Err(ValidationError::MissingField(field_name.clone()));
                }
                None => {}
            }
        }
        
        Ok(())
    }
}

fn main() {
    // Build validator
    let validator = ObjectValidator::new()
        .add_field(
            "type",
            Box::new(StringValidator {
                min_length: Some(1),
                max_length: None,
                pattern: None,
            }),
            true
        )
        .add_field(
            "user_id",
            Box::new(NumberValidator {
                min: Some(1.0),
                max: None,
            }),
            true
        )
        .add_field(
            "username",
            Box::new(StringValidator {
                min_length: Some(3),
                max_length: Some(20),
                pattern: Some(regex::Regex::new(r"^[a-zA-Z0-9_]+$").unwrap()),
            }),
            true
        );
    
    // Test validation
    let valid_msg = serde_json::json!({
        "type": "user",
        "user_id": 42,
        "username": "john_doe"
    });
    
    let invalid_msg = serde_json::json!({
        "type": "user",
        "user_id": -5,
        "username": "ab"
    });
    
    match validator.validate(&valid_msg) {
        Ok(_) => println!("✓ Valid message passed"),
        Err(e) => println!("✗ Valid message failed: {:?}", e),
    }
    
    match validator.validate(&invalid_msg) {
        Ok(_) => println!("✓ Invalid message passed"),
        Err(e) => println!("✗ Invalid message failed: {:?}", e),
    }
}
```

---

## Summary

**WebSocket Schema Validation** is essential for building robust real-time applications. Key takeaways:

### Benefits
- **Type Safety**: Prevents runtime errors from malformed data
- **Contract Enforcement**: Ensures client/server agreement on message formats
- **Early Error Detection**: Catches issues before they reach business logic
- **Documentation**: Schemas serve as machine-readable API contracts
- **Security**: Prevents injection attacks and unexpected data structures

### Implementation Approaches

**C/C++**: Uses libraries like RapidJSON with JSON Schema support or custom validation logic. More manual but highly performant.

**Rust**: Leverages type system with serde for serialization/deserialization and jsonschema for runtime validation. Provides compile-time and runtime safety.

### Best Practices
1. Define schemas early in development
2. Validate all incoming messages before processing
3. Provide clear error messages for validation failures
4. Version your schemas as APIs evolve
5. Use standard formats (JSON Schema, Protocol Buffers) for interoperability
6. Consider performance impact—cache compiled schemas
7. Balance strictness with flexibility for evolving APIs

Schema validation is a critical component of production WebSocket applications, providing the foundation for reliable, type-safe real-time communication.