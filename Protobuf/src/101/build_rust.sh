#!/bin/bash

echo "==================================="
echo "Protocol Buffers Tutorial - Quick Build"
echo "==================================="
echo ""

# Check if protoc is installed
if ! command -v protoc &> /dev/null; then
    echo "ERROR: protoc not found!"
    echo "Please install it with:"
    echo "  sudo apt install protobuf-compiler libprotobuf-dev"
    exit 1
fi

echo "✓ protoc found: $(protoc --version)"
echo ""

# Build Rust
if command -v cargo &> /dev/null; then
    echo "Building Rust project..."
    cd rust
    cargo build --release || { echo "Cargo build failed!"; exit 1; }
    echo "✓ Rust build successful"
    cd ..
else
    echo "⚠ Cargo not found, skipping Rust build"
    echo "Install Rust from: https://rustup.rs/"
fi

echo ""
echo "==================================="
echo "Build complete! Try these commands:"
echo "==================================="
echo ""
echo "  cd rust"
echo "  cargo run --bin writer --release"
echo "  cargo run --bin reader --release"
echo ""
