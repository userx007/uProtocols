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

## License

This tutorial is provided as-is for educational purposes.
