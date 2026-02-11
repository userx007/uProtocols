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
