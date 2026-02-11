## Prerequisites & Downloads

### 1. Install Protocol Buffer Compiler (protoc)

**Option A: From package manager (recommended)**
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install -y protobuf-compiler libprotobuf-dev

# Fedora/RHEL
sudo dnf install protobuf protobuf-devel

# Arch
sudo pacman -S protobuf
```

**Option B: From source (latest version)**
```bash
# Download from https://github.com/protocolbuffers/protobuf/releases
wget https://github.com/protocolbuffers/protobuf/releases/download/v25.1/protobuf-25.1.tar.gz
tar -xzf protobuf-25.1.tar.gz
cd protobuf-25.1
./configure
make -j$(nproc)
sudo make install
sudo ldconfig
```

Verify installation:
```bash
protoc --version
```

### 2. Install Build Tools

```bash
# C++ tools
sudo apt install -y build-essential cmake

# Rust tools
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

---

## Project Structure

```
protobuf-tutorial/
├── proto/
│   └── messages.proto
├── cpp/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── writer.cpp
│   │   └── reader.cpp
│   └── build/
└── rust/
    ├── Cargo.toml
    ├── build.rs
    └── src/
        ├── main.rs
        └── bin/
            ├── writer.rs
            └── reader.rs
```

---

## Step 1: Define Protocol Buffer Schema

Create `proto/messages.proto`:

```protobuf
syntax = "proto3";

package tutorial;

message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
  
  enum PhoneType {
    MOBILE = 0;
    HOME = 1;
    WORK = 2;
  }
  
  message PhoneNumber {
    string number = 1;
    PhoneType type = 2;
  }
  
  repeated PhoneNumber phones = 4;
}

message AddressBook {
  repeated Person people = 1;
}
```

---

## C++ Implementation

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.15)
project(ProtobufTutorial CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Protobuf
find_package(Protobuf REQUIRED)

# Generate C++ code from .proto files
set(PROTO_FILES ${CMAKE_SOURCE_DIR}/../proto/messages.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Include directories
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${Protobuf_INCLUDE_DIRS})

# Writer executable
add_executable(writer src/writer.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(writer ${Protobuf_LIBRARIES})

# Reader executable
add_executable(reader src/reader.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(reader ${Protobuf_LIBRARIES})
```

### src/writer.cpp

```cpp
#include <iostream>
#include <fstream>
#include "messages.pb.h"

int main() {
    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    tutorial::AddressBook address_book;
    
    // Add a person
    tutorial::Person* person = address_book.add_people();
    person->set_name("John Doe");
    person->set_id(1234);
    person->set_email("john@example.com");
    
    tutorial::Person::PhoneNumber* phone = person->add_phones();
    phone->set_number("555-1234");
    phone->set_type(tutorial::Person::MOBILE);
    
    phone = person->add_phones();
    phone->set_number("555-5678");
    phone->set_type(tutorial::Person::WORK);
    
    // Write to file
    std::fstream output("addressbook.pb", std::ios::out | std::ios::binary);
    if (!address_book.SerializeToOstream(&output)) {
        std::cerr << "Failed to write address book." << std::endl;
        return -1;
    }
    
    std::cout << "Address book written successfully!" << std::endl;
    
    // Optional: Delete all global objects allocated by libprotobuf.
    google::protobuf::ShutdownProtobufLibrary();
    
    return 0;
}
```

### src/reader.cpp

```cpp
#include <iostream>
#include <fstream>
#include "messages.pb.h"

void PrintAddressBook(const tutorial::AddressBook& address_book) {
    for (int i = 0; i < address_book.people_size(); i++) {
        const tutorial::Person& person = address_book.people(i);
        
        std::cout << "Person ID: " << person.id() << std::endl;
        std::cout << "  Name: " << person.name() << std::endl;
        std::cout << "  E-mail: " << person.email() << std::endl;
        
        for (int j = 0; j < person.phones_size(); j++) {
            const tutorial::Person::PhoneNumber& phone = person.phones(j);
            
            switch (phone.type()) {
                case tutorial::Person::MOBILE:
                    std::cout << "  Mobile phone: ";
                    break;
                case tutorial::Person::HOME:
                    std::cout << "  Home phone: ";
                    break;
                case tutorial::Person::WORK:
                    std::cout << "  Work phone: ";
                    break;
            }
            std::cout << phone.number() << std::endl;
        }
    }
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    tutorial::AddressBook address_book;
    
    std::fstream input("addressbook.pb", std::ios::in | std::ios::binary);
    if (!input) {
        std::cerr << "File not found." << std::endl;
        return -1;
    } else if (!address_book.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse address book." << std::endl;
        return -1;
    }
    
    PrintAddressBook(address_book);
    
    google::protobuf::ShutdownProtobufLibrary();
    
    return 0;
}
```

### Build and Run C++

```bash
cd cpp
mkdir -p build && cd build
cmake ..
make
./writer
./reader
```

---

## Rust Implementation

### Cargo.toml

```toml
[package]
name = "protobuf-tutorial"
version = "0.1.0"
edition = "2021"

[dependencies]
prost = "0.12"
bytes = "1.5"

[build-dependencies]
prost-build = "0.12"

[[bin]]
name = "writer"
path = "src/bin/writer.rs"

[[bin]]
name = "reader"
path = "src/bin/reader.rs"
```

### build.rs

```rust
fn main() {
    prost_build::compile_protos(&["../proto/messages.proto"], &["../proto/"])
        .unwrap();
}
```

### src/bin/writer.rs

```rust
use std::fs::File;
use std::io::Write;
use prost::Message;

// Include the generated code
pub mod tutorial {
    include!(concat!(env!("OUT_DIR"), "/tutorial.rs"));
}

use tutorial::{AddressBook, Person, person::PhoneNumber, person::PhoneType};

fn main() -> std::io::Result<()> {
    let mut address_book = AddressBook::default();
    
    let phone1 = PhoneNumber {
        number: "555-1234".to_string(),
        r#type: PhoneType::Mobile as i32,
    };
    
    let phone2 = PhoneNumber {
        number: "555-5678".to_string(),
        r#type: PhoneType::Work as i32,
    };
    
    let person = Person {
        name: "John Doe".to_string(),
        id: 1234,
        email: "john@example.com".to_string(),
        phones: vec![phone1, phone2],
    };
    
    address_book.people.push(person);
    
    // Serialize to bytes
    let mut buf = Vec::new();
    address_book.encode(&mut buf).unwrap();
    
    // Write to file
    let mut file = File::create("addressbook.pb")?;
    file.write_all(&buf)?;
    
    println!("Address book written successfully!");
    
    Ok(())
}
```

### src/bin/reader.rs

```rust
use std::fs;
use prost::Message;

pub mod tutorial {
    include!(concat!(env!("OUT_DIR"), "/tutorial.rs"));
}

use tutorial::{AddressBook, person::PhoneType};

fn main() -> std::io::Result<()> {
    let data = fs::read("addressbook.pb")?;
    
    let address_book = AddressBook::decode(&data[..])
        .expect("Failed to parse address book");
    
    for person in address_book.people {
        println!("Person ID: {}", person.id);
        println!("  Name: {}", person.name);
        println!("  E-mail: {}", person.email);
        
        for phone in person.phones {
            let phone_type = match PhoneType::try_from(phone.r#type) {
                Ok(PhoneType::Mobile) => "Mobile phone",
                Ok(PhoneType::Home) => "Home phone",
                Ok(PhoneType::Work) => "Work phone",
                _ => "Unknown",
            };
            println!("  {}: {}", phone_type, phone.number);
        }
    }
    
    Ok(())
}
```

### Build and Run Rust

```bash
cd rust
cargo build --release
cargo run --bin writer
cargo run --bin reader
```

---

## Testing Interoperability

The beauty of Protocol Buffers is language interoperability. You can write with C++ and read with Rust:

```bash
# Write with C++
cd cpp/build
./writer

# Read with Rust
cd ../../rust
cargo run --bin reader
```

Or vice versa!

---

## Key Concepts

1. **`.proto` files**: Define your data structure once, use everywhere
2. **Code generation**: `protoc` generates language-specific code
3. **Serialization**: Convert objects to binary format
4. **Deserialization**: Parse binary back to objects
5. **Binary format**: Compact, fast, backward/forward compatible

## Common Issues & Solutions

- **protoc not found**: Make sure it's in your PATH after installation
- **CMake can't find Protobuf**: Install `libprotobuf-dev`
- **Rust build fails**: Check that `prost` versions match in dependencies and build-dependencies

---

# Protocol Buffers Tutorial - C++ and Rust

A complete hands-on tutorial for learning Protocol Buffers with C++ and Rust implementations.

## Project Structure

```
protobuf-tutorial/
├── proto/
│   └── messages.proto          # Shared protocol buffer schema
├── cpp/
│   ├── CMakeLists.txt         # CMake build configuration
│   └── src/
│       ├── writer.cpp         # C++ writer example
│       └── reader.cpp         # C++ reader example
└── rust/
    ├── Cargo.toml             # Rust project configuration
    ├── build.rs               # Rust protobuf code generation
    └── src/bin/
        ├── writer.rs          # Rust writer example
        └── reader.rs          # Rust reader example
```

## Prerequisites

### 1. Install Protocol Buffer Compiler

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y protobuf-compiler libprotobuf-dev build-essential cmake
```

**Fedora/RHEL:**
```bash
sudo dnf install protobuf protobuf-devel gcc-c++ cmake
```

**Arch Linux:**
```bash
sudo pacman -S protobuf base-devel cmake
```

**Verify installation:**
```bash
protoc --version
# Should show version 3.x or higher
```

### 2. Install Rust (if not already installed)

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

## Building and Running

### C++ Implementation

```bash
# Navigate to C++ directory
cd cpp

# Create build directory
mkdir -p build && cd build

# Configure with CMake
cmake ..

# Build
make

# Run writer (creates addressbook.pb)
./writer

# Run reader (reads addressbook.pb)
./reader
```

**Expected output from writer:**
```
Address book written successfully!
```

**Expected output from reader:**
```
Person ID: 1234
  Name: John Doe
  E-mail: john@example.com
  Mobile phone: 555-1234
  Work phone: 555-5678
```

### Rust Implementation

```bash
# Navigate to Rust directory
cd rust

# Build the project (downloads dependencies and compiles)
cargo build --release

# Run writer (creates addressbook.pb)
cargo run --bin writer --release

# Run reader (reads addressbook.pb)
cargo run --bin reader --release
```

**Expected output is the same as C++ version**

## Testing Interoperability

The power of Protocol Buffers is language interoperability. Test it:

### Write with C++, Read with Rust

```bash
# In cpp/build/
./writer

# Copy the file to rust directory
cp addressbook.pb ../../rust/

# In rust/
cargo run --bin reader --release
```

### Write with Rust, Read with C++

```bash
# In rust/
cargo run --bin writer --release

# Copy the file to cpp/build/
cp addressbook.pb ../cpp/build/

# In cpp/build/
./reader
```

Both should produce identical output!

## Understanding the Code

### Protocol Buffer Schema (`proto/messages.proto`)

Defines the data structure:
- **Person**: Contains name, id, email, and phone numbers
- **PhoneNumber**: Nested message with number and type
- **PhoneType**: Enum for MOBILE, HOME, WORK
- **AddressBook**: Collection of Person messages

### Key Concepts

1. **Serialization**: Converting objects to binary format
   - C++: `SerializeToOstream()`
   - Rust: `encode()`

2. **Deserialization**: Parsing binary back to objects
   - C++: `ParseFromIstream()`
   - Rust: `decode()`

3. **Code Generation**:
   - C++: CMake calls `protoc` to generate `.pb.h` and `.pb.cc`
   - Rust: `build.rs` uses `prost-build` to generate code at compile time

## Common Issues and Solutions

### Issue: "protoc: command not found"
**Solution:** Install protobuf-compiler package (see Prerequisites)

### Issue: CMake can't find Protobuf
**Solution:** 
```bash
sudo apt install libprotobuf-dev
# or
sudo ldconfig  # After building protobuf from source
```

### Issue: Rust build fails with prost errors
**Solution:** Ensure prost and prost-build versions match in Cargo.toml

### Issue: "Permission denied" when running binaries
**Solution:**
```bash
chmod +x writer reader  # For C++
```

## Extending the Tutorial

### Add More Fields

Edit `proto/messages.proto`:
```protobuf
message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
  repeated PhoneNumber phones = 4;
  string address = 5;           // Add this
  int32 age = 6;                // Add this
}
```

Then rebuild both projects. The beauty of protobuf is backward compatibility!

### Add More Messages

```protobuf
message Company {
  string name = 1;
  repeated Person employees = 2;
}
```

## Performance Tips

1. **Reuse objects**: Don't create new message objects for each serialization
2. **Arena allocation** (C++): Use `google::protobuf::Arena` for better memory management
3. **Streaming**: For large datasets, use `ParseDelimitedFromStream()`

## Additional Resources

- [Protocol Buffers Documentation](https://protobuf.dev/)
- [Protobuf C++ Tutorial](https://protobuf.dev/getting-started/cpptutorial/)
- [Prost (Rust) Documentation](https://docs.rs/prost/)
- [Language Guide](https://protobuf.dev/programming-guides/proto3/)

## Next Steps

1. Try adding more complex nested messages
2. Experiment with different data types (bytes, maps, oneof)
3. Test with larger datasets
4. Explore gRPC (uses Protocol Buffers for service definitions)
5. Compare with JSON serialization for size and speed

