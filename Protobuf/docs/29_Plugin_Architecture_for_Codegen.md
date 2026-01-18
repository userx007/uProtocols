# Plugin Architecture for Protocol Buffer Code Generation

## Overview

Protocol Buffers (protobuf) provides an extensible plugin architecture that allows developers to create custom code generators beyond the standard language implementations. Instead of modifying the `protoc` compiler directly, you can write plugins that receive parsed proto definitions and generate custom code, documentation, validation logic, or any other artifacts needed for your project.

## How the Plugin Architecture Works

The plugin system operates through a standardized pipeline:

1. **`protoc` parses** `.proto` files into an internal representation
2. **Serializes** the parsed data as a `CodeGeneratorRequest` protobuf message
3. **Pipes** this request to your plugin via stdin
4. **Your plugin** processes the request and generates a `CodeGeneratorResponse`
5. **`protoc` writes** the generated files to disk

This architecture ensures plugins remain independent from the core compiler while maintaining access to the complete type system and metadata.

## Key Protobuf Definitions

Plugins work with messages defined in `google/protobuf/compiler/plugin.proto`:

```protobuf
message CodeGeneratorRequest {
  repeated string file_to_generate = 1;
  optional string parameter = 2;
  repeated FileDescriptorProto proto_file = 15;
  optional Version compiler_version = 3;
}

message CodeGeneratorResponse {
  optional string error = 1;
  optional uint64 supported_features = 2;
  repeated File file = 15;
  
  message File {
    optional string name = 1;
    optional string insertion_point = 2;
    optional string content = 15;
  }
}
```

## C++ Plugin Example

Here's a complete C++ plugin that generates validation code:

```cpp
#include <google/protobuf/compiler/plugin.pb.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <iostream>
#include <string>

using google::protobuf::compiler::CodeGeneratorRequest;
using google::protobuf::compiler::CodeGeneratorResponse;
using google::protobuf::FileDescriptor;
using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;

class ValidationGenerator {
public:
    bool Generate(const CodeGeneratorRequest& request,
                  CodeGeneratorResponse* response) {
        
        for (const auto& file_name : request.file_to_generate()) {
            const google::protobuf::FileDescriptorProto* file_proto = nullptr;
            
            // Find the file descriptor
            for (const auto& proto_file : request.proto_file()) {
                if (proto_file.name() == file_name) {
                    file_proto = &proto_file;
                    break;
                }
            }
            
            if (!file_proto) continue;
            
            // Generate output file
            auto* file = response->add_file();
            file->set_name(file_name + ".validator.h");
            
            std::string content;
            content += "#pragma once\n";
            content += "#include <string>\n";
            content += "#include <vector>\n\n";
            
            // Generate validator for each message
            for (const auto& message : file_proto->message_type()) {
                content += GenerateMessageValidator(message);
            }
            
            file->set_content(content);
        }
        
        return true;
    }

private:
    std::string GenerateMessageValidator(
        const google::protobuf::DescriptorProto& message) {
        
        std::string code;
        std::string class_name = message.name() + "Validator";
        
        code += "class " + class_name + " {\n";
        code += "public:\n";
        code += "    static std::vector<std::string> Validate(const " + 
                message.name() + "& msg) {\n";
        code += "        std::vector<std::string> errors;\n";
        
        // Generate validation for each field
        for (const auto& field : message.field()) {
            if (field.label() == google::protobuf::FieldDescriptorProto::LABEL_REQUIRED) {
                code += "        if (!msg.has_" + field.name() + "()) {\n";
                code += "            errors.push_back(\"Field '" + field.name() + 
                       "' is required\");\n";
                code += "        }\n";
            }
            
            // Add string length validation if custom option exists
            if (field.type() == google::protobuf::FieldDescriptorProto::TYPE_STRING) {
                code += "        if (msg." + field.name() + "().empty()) {\n";
                code += "            errors.push_back(\"Field '" + field.name() + 
                       "' cannot be empty\");\n";
                code += "        }\n";
            }
        }
        
        code += "        return errors;\n";
        code += "    }\n";
        code += "};\n\n";
        
        return code;
    }
};

int main(int argc, char* argv[]) {
    CodeGeneratorRequest request;
    CodeGeneratorResponse response;
    
    // Read request from stdin
    google::protobuf::io::FileInputStream input(STDIN_FILENO);
    if (!request.ParseFromZeroCopyStream(&input)) {
        std::cerr << "Failed to parse CodeGeneratorRequest\n";
        return 1;
    }
    
    // Generate code
    ValidationGenerator generator;
    if (!generator.Generate(request, &response)) {
        response.set_error("Code generation failed");
    }
    
    // Write response to stdout
    google::protobuf::io::FileOutputStream output(STDOUT_FILENO);
    if (!response.SerializeToZeroCopyStream(&output)) {
        std::cerr << "Failed to serialize CodeGeneratorResponse\n";
        return 1;
    }
    
    return 0;
}
```

**Compilation:**
```bash
g++ -std=c++17 -o protoc-gen-validator validator_plugin.cpp \
    -lprotobuf -lprotoc -I/usr/local/include
```

**Usage:**
```bash
protoc --plugin=protoc-gen-validator=./protoc-gen-validator \
       --validator_out=./generated \
       myproto.proto
```

## Rust Plugin Example

Here's a Rust plugin using the `prost` and `prost-types` crates:

```rust
use prost::Message;
use prost_types::compiler::{CodeGeneratorRequest, CodeGeneratorResponse};
use prost_types::{FileDescriptorProto, DescriptorProto, FieldDescriptorProto};
use std::io::{self, Read, Write};

struct DocumentationGenerator;

impl DocumentationGenerator {
    fn generate(&self, request: CodeGeneratorRequest) -> CodeGeneratorResponse {
        let mut response = CodeGeneratorResponse::default();
        
        for file_name in &request.file_to_generate {
            let file_proto = request.proto_file.iter()
                .find(|f| &f.name() == file_name);
            
            if let Some(file_proto) = file_proto {
                let mut file = prost_types::compiler::code_generator_response::File::default();
                file.name = Some(format!("{}.md", file_name));
                
                let mut content = String::new();
                content.push_str(&format!("# Protocol Buffer Documentation: {}\n\n", file_name));
                
                // Generate documentation for each message
                for message in &file_proto.message_type {
                    content.push_str(&self.generate_message_docs(message, 0));
                }
                
                file.content = Some(content);
                response.file.push(file);
            }
        }
        
        response
    }
    
    fn generate_message_docs(&self, message: &DescriptorProto, depth: usize) -> String {
        let indent = "  ".repeat(depth);
        let mut docs = String::new();
        
        docs.push_str(&format!("{}## Message: {}\n\n", indent, message.name()));
        
        // Add leading comments if available
        if let Some(source_info) = message.options.as_ref() {
            // Note: In production, you'd extract comments from SourceCodeInfo
            docs.push_str(&format!("{}*Description would go here*\n\n", indent));
        }
        
        docs.push_str(&format!("{}### Fields\n\n", indent));
        
        for field in &message.field {
            docs.push_str(&self.generate_field_docs(field, depth + 1));
        }
        
        // Handle nested messages
        for nested in &message.nested_type {
            docs.push_str(&self.generate_message_docs(nested, depth + 1));
        }
        
        docs.push_str("\n");
        docs
    }
    
    fn generate_field_docs(&self, field: &FieldDescriptorProto, depth: usize) -> String {
        let indent = "  ".repeat(depth);
        let mut docs = String::new();
        
        let field_type = self.format_field_type(field);
        let label = self.format_label(field.label());
        
        docs.push_str(&format!(
            "{}- **{}** (`{} {}`): Field number {}\n",
            indent,
            field.name(),
            label,
            field_type,
            field.number()
        ));
        
        docs
    }
    
    fn format_field_type(&self, field: &FieldDescriptorProto) -> String {
        use prost_types::field_descriptor_proto::Type;
        
        match field.r#type() {
            Type::Double => "double".to_string(),
            Type::Float => "float".to_string(),
            Type::Int32 => "int32".to_string(),
            Type::Int64 => "int64".to_string(),
            Type::String => "string".to_string(),
            Type::Bool => "bool".to_string(),
            Type::Message => field.type_name().trim_start_matches('.').to_string(),
            _ => "unknown".to_string(),
        }
    }
    
    fn format_label(&self, label: i32) -> &str {
        use prost_types::field_descriptor_proto::Label;
        
        match Label::try_from(label) {
            Ok(Label::Optional) => "optional",
            Ok(Label::Required) => "required",
            Ok(Label::Repeated) => "repeated",
            _ => "",
        }
    }
}

fn main() -> io::Result<()> {
    // Read request from stdin
    let mut buffer = Vec::new();
    io::stdin().read_to_end(&mut buffer)?;
    
    let request = CodeGeneratorRequest::decode(&buffer[..])
        .expect("Failed to parse CodeGeneratorRequest");
    
    // Generate documentation
    let generator = DocumentationGenerator;
    let response = generator.generate(request);
    
    // Write response to stdout
    let mut output = Vec::new();
    response.encode(&mut output)
        .expect("Failed to encode CodeGeneratorResponse");
    
    io::stdout().write_all(&output)?;
    
    Ok(())
}
```

**Cargo.toml:**
```toml
[package]
name = "protoc-gen-doc"
version = "0.1.0"
edition = "2021"

[dependencies]
prost = "0.12"
prost-types = "0.12"
```

**Build and Usage:**
```bash
cargo build --release
cp target/release/protoc-gen-doc /usr/local/bin/

protoc --plugin=protoc-gen-doc \
       --doc_out=./docs \
       *.proto
```

## Advanced: C Plugin with Custom Options

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <google/protobuf-c/protobuf-c.h>
#include "google/protobuf/compiler/plugin.pb-c.h"

typedef struct {
    char* filename;
    char* content;
} GeneratedFile;

void generate_json_schema(
    Google__Protobuf__Compiler__CodeGeneratorRequest* request,
    Google__Protobuf__Compiler__CodeGeneratorResponse* response) {
    
    for (size_t i = 0; i < request->n_file_to_generate; i++) {
        const char* file_name = request->file_to_generate[i];
        
        // Find corresponding FileDescriptorProto
        Google__Protobuf__FileDescriptorProto* file_proto = NULL;
        for (size_t j = 0; j < request->n_proto_file; j++) {
            if (strcmp(request->proto_file[j]->name, file_name) == 0) {
                file_proto = request->proto_file[j];
                break;
            }
        }
        
        if (!file_proto) continue;
        
        // Allocate response file
        response->n_file++;
        response->file = realloc(response->file, 
            sizeof(Google__Protobuf__Compiler__CodeGeneratorResponse__File*) * response->n_file);
        
        Google__Protobuf__Compiler__CodeGeneratorResponse__File* out_file = 
            malloc(sizeof(Google__Protobuf__Compiler__CodeGeneratorResponse__File));
        
        google__protobuf__compiler__code_generator_response__file__init(out_file);
        
        // Set output filename
        char output_name[256];
        snprintf(output_name, sizeof(output_name), "%s.schema.json", file_name);
        out_file->name = strdup(output_name);
        
        // Generate JSON schema content
        char content[4096] = "{\n  \"$schema\": \"http://json-schema.org/draft-07/schema#\",\n";
        strcat(content, "  \"definitions\": {\n");
        
        // Add message definitions (simplified)
        for (size_t m = 0; m < file_proto->n_message_type; m++) {
            // Schema generation logic here
        }
        
        strcat(content, "  }\n}\n");
        out_file->content = strdup(content);
        
        response->file[response->n_file - 1] = out_file;
    }
}

int main(int argc, char** argv) {
    // Read from stdin
    uint8_t buffer[1024 * 1024];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), stdin);
    
    Google__Protobuf__Compiler__CodeGeneratorRequest* request = 
        google__protobuf__compiler__code_generator_request__unpack(NULL, bytes_read, buffer);
    
    if (!request) {
        fprintf(stderr, "Failed to parse request\n");
        return 1;
    }
    
    // Prepare response
    Google__Protobuf__Compiler__CodeGeneratorResponse response = 
        GOOGLE__PROTOBUF__COMPILER__CODE_GENERATOR_RESPONSE__INIT;
    
    generate_json_schema(request, &response);
    
    // Serialize and write to stdout
    size_t response_size = google__protobuf__compiler__code_generator_response__get_packed_size(&response);
    uint8_t* response_buffer = malloc(response_size);
    google__protobuf__compiler__code_generator_response__pack(&response, response_buffer);
    
    fwrite(response_buffer, 1, response_size, stdout);
    
    // Cleanup
    free(response_buffer);
    google__protobuf__compiler__code_generator_request__free_unpacked(request, NULL);
    
    return 0;
}
```

## Summary

**Protocol Buffer's plugin architecture** enables extending code generation without modifying the core compiler. Key points:

- **Stdin/Stdout Protocol**: Plugins receive `CodeGeneratorRequest` via stdin and return `CodeGeneratorResponse` via stdout
- **Language Agnostic**: Write plugins in any language that can serialize/deserialize protobuf messages
- **Rich Metadata**: Access to complete type information, comments, options, and file structure
- **Multiple Outputs**: Generate code, documentation, validation, schemas, or any custom artifacts
- **Naming Convention**: Plugins must be named `protoc-gen-<NAME>` and invoked with `--<NAME>_out`
- **Common Use Cases**: Custom validators, API documentation, test data generators, ORM mappings, GraphQL schemas

This extensibility makes Protocol Buffers adaptable to diverse workflows while maintaining type safety and consistency across generated artifacts.