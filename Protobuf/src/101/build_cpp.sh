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

# Build C++
echo "Building C++ project..."
cd cpp
mkdir -p build
cd build
cmake .. || { echo "CMake failed!"; exit 1; }
make || { echo "Make failed!"; exit 1; }
echo "✓ C++ build successful"
echo ""

# Return to root
cd ../..

echo ""
echo "==================================="
echo "Build complete! Try these commands:"
echo "==================================="
echo ""
echo "C++:"
echo "  cd cpp/build"
echo "  ./writer"
echo "  ./reader"
echo ""
