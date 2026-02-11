# CommonAPI: A Comprehensive Guide
**Inter-Process Communication Framework for Automotive and Embedded Systems**

**Main Sections:**
- Introduction and architecture overview
- Core components (FIDL, Proxy, Stub, Runtime)
- Protocol bindings (D-Bus and SOME/IP)
- Complete working examples in C++ (primary language)
- C integration through wrapper functions
- Rust bindings using FFI
- Advanced features (managed interfaces, complex types, selective broadcasts)
- Best practices for production use
- Real-world use cases

**Code Examples Include:**
- Full service and client implementations in C++
- C wrapper library with complete client example
- Rust FFI bindings with safe wrapper and client
- Build configurations (CMake and Cargo)
- Error handling patterns
- Asynchronous method calls
- Event subscriptions
- Attribute management

The guide covers everything from basic concepts to advanced patterns used in automotive and embedded systems development.

---

## Table of Contents
1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Core Components](#core-components)
4. [Protocol Bindings](#protocol-bindings)
5. [C++ Implementation](#cpp-implementation)
6. [C Integration](#c-integration)
7. [Rust Bindings](#rust-bindings)
8. [Advanced Features](#advanced-features)
9. [Best Practices](#best-practices)
10.[Real-World Use Cases](#real-world-use-cases)

---

## Introduction

### What is CommonAPI?

CommonAPI is a C++-based Inter-Process Communication (IPC) framework primarily used in automotive and embedded systems. It provides a language-independent middleware abstraction layer that enables communication between different software components across process boundaries.

**Key Features:**
- **Protocol Independence**: Supports multiple IPC mechanisms (D-Bus, SOME/IP)
- **Code Generation**: Automatic generation of proxy and stub code from FIDL interface definitions
- **Type Safety**: Strong typing with automatic marshalling/unmarshalling
- **Asynchronous Communication**: Built-in support for async method calls and callbacks
- **Broadcasting**: Selective and broadcast events
- **Version Management**: Interface versioning support

### Why CommonAPI?

Traditional IPC mechanisms require developers to handle low-level details like message serialization, connection management, and error handling. CommonAPI abstracts these complexities by providing:

1. **High-level API**: Focus on business logic rather than IPC mechanics
2. **Multiple Backend Support**: Switch between D-Bus and SOME/IP without changing application code
3. **Standardization**: Common interface definition language (Franca IDL)
4. **Tool Support**: Code generators for client and server implementations

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                   Application Layer                     │
│  ┌──────────────┐              ┌──────────────┐         │
│  │   Client     │              │   Service    │         │
│  │  (Proxy)     │              │   (Stub)     │         │
│  └──────┬───────┘              └───────┬──────┘         │
│         │                              │                │
├─────────┼──────────────────────────────┼────────────────┤
│         │    CommonAPI Runtime Layer   │                │
│  ┌──────▼──────────────────────────────▼──────┐         │
│  │      CommonAPI Core (libCommonAPI)         │         │
│  │  - Connection Management                   │         │
│  │  - Method Invocation                       │         │
│  │  - Event Handling                          │         │
│  │  - Attribute Management                    │         │
│  └──────┬─────────────────────────────┬───────┘         │
│         │                             │                 │
├─────────┼─────────────────────────────┼─────────────────┤
│         │   Binding Layer             │                 │
│  ┌──────▼──────┐              ┌───────▼──────┐          │
│  │  D-Bus      │              │   SOME/IP    │          │
│  │  Binding    │              │   Binding    │          │
│  └──────┬──────┘              └───────┬──────┘          │
│         │                             │                 │
├─────────┼─────────────────────────────┼─────────────────┤
│         │  Transport Layer            │                 │
│  ┌──────▼──────┐              ┌───────▼──────┐          │
│  │   D-Bus     │              │   UDP/TCP    │          │
│  │   Daemon    │              │              │          │
│  └─────────────┘              └──────────────┘          │
└─────────────────────────────────────────────────────────┘
```

### Component Layers

1. **Application Layer**: Your client (proxy) and service (stub) implementations
2. **CommonAPI Core**: Framework runtime providing the high-level API
3. **Binding Layer**: Protocol-specific implementations (D-Bus, SOME/IP)
4. **Transport Layer**: Underlying IPC mechanisms

---

## Core Components

### 1. Franca IDL (Interface Definition Language)

Franca IDL is used to define service interfaces in a protocol-agnostic way.

**Example Interface Definition (Calculator.fidl):**

```fidl
package org.example

interface Calculator {
    version { major 1 minor 0 }
    
    // Attributes (stateful properties)
    attribute Int32 lastResult readonly
    
    // Methods
    method add {
        in {
            Int32 a
            Int32 b
        }
        out {
            Int32 result
        }
    }
    
    method divide {
        in {
            Int32 dividend
            Int32 divisor
        }
        out {
            Int32 result
        }
        error {
            DIVISION_BY_ZERO
            OVERFLOW
        }
    }
    
    // Broadcasts (events)
    broadcast calculationPerformed {
        out {
            String operation
            Int32 result
        }
    }
    
    // Selective broadcasts
    broadcast errorOccurred selective {
        out {
            String errorMessage
        }
    }
}
```

### 2. Proxy (Client Side)

The proxy represents the client-side interface. It's generated from the FIDL definition and provides methods to:
- Call remote methods (synchronously or asynchronously)
- Subscribe to broadcasts/events
- Get/set attributes

### 3. Stub (Service Side)

The stub is the server-side skeleton. Developers implement the actual service logic by inheriting from the generated stub class.

### 4. Runtime

The CommonAPI runtime manages:
- Service discovery and registration
- Connection lifecycle
- Thread management
- Dispatching of events and callbacks

---

## Protocol Bindings

### D-Bus Binding

D-Bus is suitable for:
- Single ECU (Electronic Control Unit) communication
- Desktop applications
- Low to medium throughput requirements

**Configuration (commonapi-dbus.ini):**

```ini
[local]
default = dbus.conf

[dbus.conf]
org.example.Calculator=Calculator
```

### SOME/IP Binding

SOME/IP (Scalable service-Oriented MiddlewarE over IP) is designed for:
- Automotive Ethernet
- High-performance scenarios
- ECU-to-ECU communication over network

**Configuration (commonapi-someip.ini):**

```ini
[local]
default = someip.json

[someip.json]
{
    "services": [
        {
            "service": "0x1234",
            "instance": "0x5678",
            "reliable": { "port": 30500 }
        }
    ]
}
```

---

## C++ Implementation

### Complete Service Implementation

**CalculatorStubImpl.hpp:**

```cpp
#ifndef CALCULATOR_STUB_IMPL_HPP
#define CALCULATOR_STUB_IMPL_HPP

#include <CommonAPI/CommonAPI.hpp>
#include <v1/org/example/CalculatorStubDefault.hpp>
#include <iostream>

namespace v1 {
namespace org {
namespace example {

class CalculatorStubImpl : public CalculatorStubDefault {
public:
    CalculatorStubImpl() : lastResult_(0) {
        std::cout << "Calculator service initialized" << std::endl;
    }
    
    virtual ~CalculatorStubImpl() = default;
    
    // Implement the add method
    virtual void add(const std::shared_ptr<CommonAPI::ClientId> _client,
                    int32_t _a,
                    int32_t _b,
                    addReply_t _reply) override {
        
        int32_t result = _a + _b;
        lastResult_ = result;
        
        // Update the attribute
        setLastResultAttribute(result);
        
        // Fire the broadcast
        std::string operation = "ADD";
        fireCalculationPerformedEvent(operation, result);
        
        // Send reply to client
        _reply(result);
        
        std::cout << "Addition: " << _a << " + " << _b 
                  << " = " << result << std::endl;
    }
    
    // Implement the divide method with error handling
    virtual void divide(const std::shared_ptr<CommonAPI::ClientId> _client,
                       int32_t _dividend,
                       int32_t _divisor,
                       divideReply_t _reply) override {
        
        if (_divisor == 0) {
            // Return error
            _reply(CalculatorError::DIVISION_BY_ZERO, 0);
            
            // Fire selective broadcast
            fireErrorOccurredEvent("Division by zero attempted");
            
            std::cerr << "Error: Division by zero" << std::endl;
            return;
        }
        
        // Check for overflow
        if (_dividend == INT32_MIN && _divisor == -1) {
            _reply(CalculatorError::OVERFLOW, 0);
            fireErrorOccurredEvent("Integer overflow in division");
            std::cerr << "Error: Overflow" << std::endl;
            return;
        }
        
        int32_t result = _dividend / _divisor;
        lastResult_ = result;
        
        setLastResultAttribute(result);
        
        std::string operation = "DIVIDE";
        fireCalculationPerformedEvent(operation, result);
        
        // Success - use default (no error) constructor
        _reply(result);
        
        std::cout << "Division: " << _dividend << " / " << _divisor 
                  << " = " << result << std::endl;
    }
    
    // Attribute getter
    virtual const int32_t& getLastResultAttribute(
        const std::shared_ptr<CommonAPI::ClientId> _client) override {
        return lastResult_;
    }

private:
    int32_t lastResult_;
};

} // namespace example
} // namespace org
} // namespace v1

#endif // CALCULATOR_STUB_IMPL_HPP
```

**Service Main (CalculatorService.cpp):**

```cpp
#include <CommonAPI/CommonAPI.hpp>
#include "CalculatorStubImpl.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace v1::org::example;

int main() {
    std::cout << "Starting Calculator Service..." << std::endl;
    
    // Get the CommonAPI runtime instance
    std::shared_ptr<CommonAPI::Runtime> runtime = 
        CommonAPI::Runtime::get();
    
    // Create service instance
    std::shared_ptr<CalculatorStubImpl> calculatorService = 
        std::make_shared<CalculatorStubImpl>();
    
    // Register service
    const std::string domain = "local";
    const std::string instance = "org.example.Calculator";
    const std::string connection = "service-sample";
    
    bool success = runtime->registerService(
        domain,
        instance,
        calculatorService,
        connection
    );
    
    if (!success) {
        std::cerr << "Failed to register service!" << std::endl;
        return 1;
    }
    
    std::cout << "Service registered successfully: " << instance << std::endl;
    std::cout << "Waiting for client requests..." << std::endl;
    
    // Keep the service running
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

### Complete Client Implementation

**CalculatorClient.cpp:**

```cpp
#include <CommonAPI/CommonAPI.hpp>
#include <v1/org/example/CalculatorProxy.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace v1::org::example;

class CalculatorClient {
public:
    CalculatorClient() {
        runtime_ = CommonAPI::Runtime::get();
        
        // Build the proxy
        const std::string domain = "local";
        const std::string instance = "org.example.Calculator";
        const std::string connection = "client-sample";
        
        proxy_ = runtime_->buildProxy<CalculatorProxy>(
            domain, 
            instance,
            connection
        );
        
        std::cout << "Waiting for service to become available..." << std::endl;
        
        // Wait for service availability
        while (!proxy_->isAvailable()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Service is available!" << std::endl;
        
        // Subscribe to broadcasts
        subscribeToBroadcasts();
        
        // Subscribe to attribute changes
        subscribeToAttributes();
    }
    
    void subscribeToBroadcasts() {
        // Subscribe to calculationPerformed broadcast
        proxy_->getCalculationPerformedEvent().subscribe(
            [](const std::string& operation, int32_t result) {
                std::cout << "[Event] Calculation performed: " 
                         << operation << " = " << result << std::endl;
            }
        );
        
        // Subscribe to errorOccurred selective broadcast
        proxy_->getErrorOccurredSelectiveEvent().subscribe(
            [](const std::string& errorMessage) {
                std::cout << "[Event] Error occurred: " 
                         << errorMessage << std::endl;
            }
        );
        
        std::cout << "Subscribed to broadcasts" << std::endl;
    }
    
    void subscribeToAttributes() {
        // Subscribe to lastResult attribute changes
        proxy_->getLastResultAttribute().getChangedEvent().subscribe(
            [](const int32_t& value) {
                std::cout << "[Attribute] Last result changed to: " 
                         << value << std::endl;
            }
        );
        
        std::cout << "Subscribed to attribute changes" << std::endl;
    }
    
    // Synchronous method call
    void addSync(int32_t a, int32_t b) {
        std::cout << "\n=== Synchronous Add ===" << std::endl;
        std::cout << "Calling add(" << a << ", " << b << ")..." << std::endl;
        
        CommonAPI::CallStatus callStatus;
        int32_t result;
        
        proxy_->add(a, b, callStatus, result);
        
        if (callStatus == CommonAPI::CallStatus::SUCCESS) {
            std::cout << "Result: " << result << std::endl;
        } else {
            std::cout << "Call failed with status: " 
                     << static_cast<int>(callStatus) << std::endl;
        }
    }
    
    // Asynchronous method call
    void addAsync(int32_t a, int32_t b) {
        std::cout << "\n=== Asynchronous Add ===" << std::endl;
        std::cout << "Calling add(" << a << ", " << b << ") async..." 
                  << std::endl;
        
        auto callback = [a, b](
            const CommonAPI::CallStatus& callStatus,
            const int32_t& result) {
            
            if (callStatus == CommonAPI::CallStatus::SUCCESS) {
                std::cout << "[Async Callback] " << a << " + " << b 
                         << " = " << result << std::endl;
            } else {
                std::cout << "[Async Callback] Call failed" << std::endl;
            }
        };
        
        proxy_->addAsync(a, b, callback);
        std::cout << "Async call initiated, continuing..." << std::endl;
    }
    
    // Method call with error handling
    void divideSync(int32_t dividend, int32_t divisor) {
        std::cout << "\n=== Synchronous Divide ===" << std::endl;
        std::cout << "Calling divide(" << dividend << ", " 
                  << divisor << ")..." << std::endl;
        
        CommonAPI::CallStatus callStatus;
        Calculator::divideError error;
        int32_t result;
        
        proxy_->divide(dividend, divisor, callStatus, error, result);
        
        if (callStatus == CommonAPI::CallStatus::SUCCESS) {
            if (error == Calculator::divideError::Literal::NO_ERROR) {
                std::cout << "Result: " << result << std::endl;
            } else if (error == Calculator::divideError::DIVISION_BY_ZERO) {
                std::cout << "Error: Division by zero!" << std::endl;
            } else if (error == Calculator::divideError::OVERFLOW) {
                std::cout << "Error: Integer overflow!" << std::endl;
            }
        } else {
            std::cout << "Call failed with status: " 
                     << static_cast<int>(callStatus) << std::endl;
        }
    }
    
    // Get attribute value
    void getLastResult() {
        std::cout << "\n=== Get Attribute ===" << std::endl;
        
        CommonAPI::CallStatus callStatus;
        int32_t value;
        
        proxy_->getLastResultAttribute().getValue(callStatus, value);
        
        if (callStatus == CommonAPI::CallStatus::SUCCESS) {
            std::cout << "Last result: " << value << std::endl;
        } else {
            std::cout << "Failed to get attribute" << std::endl;
        }
    }

private:
    std::shared_ptr<CommonAPI::Runtime> runtime_;
    std::shared_ptr<CalculatorProxy<>> proxy_;
};

int main() {
    std::cout << "Starting Calculator Client..." << std::endl;
    
    CalculatorClient client;
    
    // Give broadcasts time to be fully subscribed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Demonstrate synchronous calls
    client.addSync(10, 20);
    client.addSync(100, 250);
    
    // Demonstrate asynchronous calls
    client.addAsync(5, 15);
    client.addAsync(33, 67);
    
    // Wait for async callbacks
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Demonstrate division
    client.divideSync(100, 4);
    client.divideSync(50, 0);  // This will trigger error
    
    // Get attribute value
    client.getLastResult();
    
    // Keep client alive to receive broadcasts
    std::cout << "\nListening for broadcasts..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    return 0;
}
```

### Build Configuration (CMakeLists.txt)

```cmake
cmake_minimum_required(VERSION 3.10)
project(CalculatorExample)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find CommonAPI packages
find_package(CommonAPI 3.2 REQUIRED)
find_package(CommonAPI-DBus 3.2 REQUIRED)

# Include generated code directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src-gen
    ${CommonAPI_INCLUDE_DIRS}
    ${CommonAPI-DBus_INCLUDE_DIRS}
)

# Service executable
add_executable(CalculatorService
    src/CalculatorService.cpp
    src/CalculatorStubImpl.cpp
)

target_link_libraries(CalculatorService
    CommonAPI
    CommonAPI-DBus
)

# Client executable
add_executable(CalculatorClient
    src/CalculatorClient.cpp
)

target_link_libraries(CalculatorClient
    CommonAPI
    CommonAPI-DBus
)
```

---

## C Integration

While CommonAPI is primarily a C++ framework, you can integrate it with C code through wrapper functions.

### C Wrapper Header (calculator_c_wrapper.h)

```c
#ifndef CALCULATOR_C_WRAPPER_H
#define CALCULATOR_C_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Opaque handle to C++ client
typedef void* CalculatorClientHandle;

// Error codes matching FIDL definition
typedef enum {
    CALC_ERROR_NONE = 0,
    CALC_ERROR_DIVISION_BY_ZERO = 1,
    CALC_ERROR_OVERFLOW = 2,
    CALC_ERROR_CALL_FAILED = 100
} CalculatorError;

// Callback types
typedef void (*CalculationPerformedCallback)(const char* operation, 
                                             int32_t result, 
                                             void* user_data);
typedef void (*ErrorOccurredCallback)(const char* error_message, 
                                      void* user_data);
typedef void (*AttributeChangedCallback)(int32_t new_value, 
                                        void* user_data);

// Client lifecycle
CalculatorClientHandle calculator_client_create(void);
void calculator_client_destroy(CalculatorClientHandle handle);
bool calculator_client_wait_available(CalculatorClientHandle handle, 
                                      uint32_t timeout_ms);

// Method calls
int32_t calculator_add(CalculatorClientHandle handle, 
                       int32_t a, 
                       int32_t b, 
                       CalculatorError* error);

int32_t calculator_divide(CalculatorClientHandle handle,
                         int32_t dividend,
                         int32_t divisor,
                         CalculatorError* error);

// Asynchronous method calls
typedef void (*AddAsyncCallback)(int32_t result, 
                                CalculatorError error, 
                                void* user_data);

void calculator_add_async(CalculatorClientHandle handle,
                         int32_t a,
                         int32_t b,
                         AddAsyncCallback callback,
                         void* user_data);

// Attribute access
int32_t calculator_get_last_result(CalculatorClientHandle handle,
                                   CalculatorError* error);

// Event subscriptions
void calculator_subscribe_calculation_performed(
    CalculatorClientHandle handle,
    CalculationPerformedCallback callback,
    void* user_data);

void calculator_subscribe_error_occurred(
    CalculatorClientHandle handle,
    ErrorOccurredCallback callback,
    void* user_data);

void calculator_subscribe_attribute_changed(
    CalculatorClientHandle handle,
    AttributeChangedCallback callback,
    void* user_data);

#ifdef __cplusplus
}
#endif

#endif // CALCULATOR_C_WRAPPER_H
```

### C Wrapper Implementation (calculator_c_wrapper.cpp)

```cpp
#include "calculator_c_wrapper.h"
#include <CommonAPI/CommonAPI.hpp>
#include <v1/org/example/CalculatorProxy.hpp>
#include <memory>
#include <string>
#include <chrono>

using namespace v1::org::example;

// Internal wrapper class
class CalculatorClientWrapper {
public:
    CalculatorClientWrapper() {
        runtime_ = CommonAPI::Runtime::get();
        proxy_ = runtime_->buildProxy<CalculatorProxy>(
            "local",
            "org.example.Calculator",
            "c-wrapper-client"
        );
    }
    
    std::shared_ptr<CalculatorProxy<>> getProxy() {
        return proxy_;
    }
    
    bool waitAvailable(uint32_t timeout_ms) {
        auto start = std::chrono::steady_clock::now();
        while (!proxy_->isAvailable()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start).count();
            
            if (elapsed >= timeout_ms) {
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

private:
    std::shared_ptr<CommonAPI::Runtime> runtime_;
    std::shared_ptr<CalculatorProxy<>> proxy_;
};

// Convert C++ error to C error
static CalculatorError convertError(const Calculator::divideError& error) {
    if (error == Calculator::divideError::DIVISION_BY_ZERO) {
        return CALC_ERROR_DIVISION_BY_ZERO;
    } else if (error == Calculator::divideError::OVERFLOW) {
        return CALC_ERROR_OVERFLOW;
    }
    return CALC_ERROR_NONE;
}

// Implementation of C interface
extern "C" {

CalculatorClientHandle calculator_client_create(void) {
    try {
        return new CalculatorClientWrapper();
    } catch (...) {
        return nullptr;
    }
}

void calculator_client_destroy(CalculatorClientHandle handle) {
    if (handle) {
        delete static_cast<CalculatorClientWrapper*>(handle);
    }
}

bool calculator_client_wait_available(CalculatorClientHandle handle, 
                                      uint32_t timeout_ms) {
    if (!handle) return false;
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    return wrapper->waitAvailable(timeout_ms);
}

int32_t calculator_add(CalculatorClientHandle handle,
                       int32_t a,
                       int32_t b,
                       CalculatorError* error) {
    if (!handle) {
        if (error) *error = CALC_ERROR_CALL_FAILED;
        return 0;
    }
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    auto proxy = wrapper->getProxy();
    
    CommonAPI::CallStatus callStatus;
    int32_t result;
    
    proxy->add(a, b, callStatus, result);
    
    if (callStatus == CommonAPI::CallStatus::SUCCESS) {
        if (error) *error = CALC_ERROR_NONE;
        return result;
    } else {
        if (error) *error = CALC_ERROR_CALL_FAILED;
        return 0;
    }
}

int32_t calculator_divide(CalculatorClientHandle handle,
                         int32_t dividend,
                         int32_t divisor,
                         CalculatorError* error) {
    if (!handle) {
        if (error) *error = CALC_ERROR_CALL_FAILED;
        return 0;
    }
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    auto proxy = wrapper->getProxy();
    
    CommonAPI::CallStatus callStatus;
    Calculator::divideError divError;
    int32_t result;
    
    proxy->divide(dividend, divisor, callStatus, divError, result);
    
    if (callStatus == CommonAPI::CallStatus::SUCCESS) {
        if (error) *error = convertError(divError);
        return result;
    } else {
        if (error) *error = CALC_ERROR_CALL_FAILED;
        return 0;
    }
}

void calculator_add_async(CalculatorClientHandle handle,
                         int32_t a,
                         int32_t b,
                         AddAsyncCallback callback,
                         void* user_data) {
    if (!handle || !callback) return;
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    auto proxy = wrapper->getProxy();
    
    proxy->addAsync(a, b, 
        [callback, user_data](const CommonAPI::CallStatus& callStatus,
                             const int32_t& result) {
            CalculatorError error = (callStatus == CommonAPI::CallStatus::SUCCESS)
                ? CALC_ERROR_NONE : CALC_ERROR_CALL_FAILED;
            callback(result, error, user_data);
        });
}

int32_t calculator_get_last_result(CalculatorClientHandle handle,
                                   CalculatorError* error) {
    if (!handle) {
        if (error) *error = CALC_ERROR_CALL_FAILED;
        return 0;
    }
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    auto proxy = wrapper->getProxy();
    
    CommonAPI::CallStatus callStatus;
    int32_t value;
    
    proxy->getLastResultAttribute().getValue(callStatus, value);
    
    if (callStatus == CommonAPI::CallStatus::SUCCESS) {
        if (error) *error = CALC_ERROR_NONE;
        return value;
    } else {
        if (error) *error = CALC_ERROR_CALL_FAILED;
        return 0;
    }
}

void calculator_subscribe_calculation_performed(
    CalculatorClientHandle handle,
    CalculationPerformedCallback callback,
    void* user_data) {
    
    if (!handle || !callback) return;
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    auto proxy = wrapper->getProxy();
    
    proxy->getCalculationPerformedEvent().subscribe(
        [callback, user_data](const std::string& operation, int32_t result) {
            callback(operation.c_str(), result, user_data);
        });
}

void calculator_subscribe_error_occurred(
    CalculatorClientHandle handle,
    ErrorOccurredCallback callback,
    void* user_data) {
    
    if (!handle || !callback) return;
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    auto proxy = wrapper->getProxy();
    
    proxy->getErrorOccurredSelectiveEvent().subscribe(
        [callback, user_data](const std::string& errorMessage) {
            callback(errorMessage.c_str(), user_data);
        });
}

void calculator_subscribe_attribute_changed(
    CalculatorClientHandle handle,
    AttributeChangedCallback callback,
    void* user_data) {
    
    if (!handle || !callback) return;
    
    auto wrapper = static_cast<CalculatorClientWrapper*>(handle);
    auto proxy = wrapper->getProxy();
    
    proxy->getLastResultAttribute().getChangedEvent().subscribe(
        [callback, user_data](const int32_t& value) {
            callback(value, user_data);
        });
}

} // extern "C"
```

### C Client Example (calculator_client.c)

```c
#include "calculator_c_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Callback implementations
void on_calculation_performed(const char* operation, 
                              int32_t result, 
                              void* user_data) {
    printf("[Event] Calculation: %s = %d\n", operation, result);
}

void on_error_occurred(const char* error_message, void* user_data) {
    printf("[Event] Error: %s\n", error_message);
}

void on_attribute_changed(int32_t new_value, void* user_data) {
    printf("[Attribute] Last result changed to: %d\n", new_value);
}

void on_add_complete(int32_t result, CalculatorError error, void* user_data) {
    int* request_id = (int*)user_data;
    printf("[Async] Request %d completed: result = %d, error = %d\n",
           *request_id, result, error);
    free(request_id);
}

int main(void) {
    printf("Starting Calculator C Client...\n");
    
    // Create client
    CalculatorClientHandle client = calculator_client_create();
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    // Wait for service
    printf("Waiting for service...\n");
    if (!calculator_client_wait_available(client, 5000)) {
        fprintf(stderr, "Service not available\n");
        calculator_client_destroy(client);
        return 1;
    }
    
    printf("Service is available!\n");
    
    // Subscribe to events
    calculator_subscribe_calculation_performed(client, 
                                              on_calculation_performed, 
                                              NULL);
    calculator_subscribe_error_occurred(client, on_error_occurred, NULL);
    calculator_subscribe_attribute_changed(client, 
                                          on_attribute_changed, 
                                          NULL);
    
    sleep(1); // Give subscriptions time to establish
    
    // Synchronous calls
    printf("\n=== Synchronous Operations ===\n");
    
    CalculatorError error;
    int32_t result = calculator_add(client, 10, 20, &error);
    if (error == CALC_ERROR_NONE) {
        printf("10 + 20 = %d\n", result);
    } else {
        printf("Addition failed with error: %d\n", error);
    }
    
    result = calculator_divide(client, 100, 4, &error);
    if (error == CALC_ERROR_NONE) {
        printf("100 / 4 = %d\n", result);
    } else {
        printf("Division result: error = %d\n", error);
    }
    
    result = calculator_divide(client, 50, 0, &error);
    if (error == CALC_ERROR_DIVISION_BY_ZERO) {
        printf("Division by zero detected (expected)\n");
    }
    
    // Asynchronous calls
    printf("\n=== Asynchronous Operations ===\n");
    
    int* req1 = malloc(sizeof(int));
    *req1 = 1;
    calculator_add_async(client, 5, 15, on_add_complete, req1);
    
    int* req2 = malloc(sizeof(int));
    *req2 = 2;
    calculator_add_async(client, 33, 67, on_add_complete, req2);
    
    // Get attribute
    printf("\n=== Attribute Access ===\n");
    result = calculator_get_last_result(client, &error);
    if (error == CALC_ERROR_NONE) {
        printf("Last result attribute: %d\n", result);
    }
    
    // Wait for async callbacks and broadcasts
    printf("\nWaiting for events...\n");
    sleep(3);
    
    // Cleanup
    calculator_client_destroy(client);
    printf("\nClient terminated\n");
    
    return 0;
}
```

### C Build Configuration

```cmake
# Add to CMakeLists.txt

# C wrapper library
add_library(calculator_c_wrapper SHARED
    src/calculator_c_wrapper.cpp
)

target_link_libraries(calculator_c_wrapper
    CommonAPI
    CommonAPI-DBus
)

# C client executable
add_executable(CalculatorCClient
    src/calculator_client.c
)

target_link_libraries(CalculatorCClient
    calculator_c_wrapper
)
```

---

## Rust Bindings

While CommonAPI doesn't have official Rust bindings, you can interface with it using FFI (Foreign Function Interface) through the C wrapper.

### Rust FFI Bindings (calculator_ffi.rs)

```rust
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

// Opaque type matching C handle
#[repr(C)]
pub struct CalculatorClientHandle {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CalculatorError {
    None = 0,
    DivisionByZero = 1,
    Overflow = 2,
    CallFailed = 100,
}

// Callback types
pub type CalculationPerformedCallback = 
    extern "C" fn(*const c_char, i32, *mut c_void);
pub type ErrorOccurredCallback = 
    extern "C" fn(*const c_char, *mut c_void);
pub type AttributeChangedCallback = 
    extern "C" fn(i32, *mut c_void);
pub type AddAsyncCallback = 
    extern "C" fn(i32, CalculatorError, *mut c_void);

// External C functions
extern "C" {
    pub fn calculator_client_create() -> *mut CalculatorClientHandle;
    pub fn calculator_client_destroy(handle: *mut CalculatorClientHandle);
    pub fn calculator_client_wait_available(
        handle: *mut CalculatorClientHandle,
        timeout_ms: u32,
    ) -> bool;
    
    pub fn calculator_add(
        handle: *mut CalculatorClientHandle,
        a: i32,
        b: i32,
        error: *mut CalculatorError,
    ) -> i32;
    
    pub fn calculator_divide(
        handle: *mut CalculatorClientHandle,
        dividend: i32,
        divisor: i32,
        error: *mut CalculatorError,
    ) -> i32;
    
    pub fn calculator_add_async(
        handle: *mut CalculatorClientHandle,
        a: i32,
        b: i32,
        callback: AddAsyncCallback,
        user_data: *mut c_void,
    );
    
    pub fn calculator_get_last_result(
        handle: *mut CalculatorClientHandle,
        error: *mut CalculatorError,
    ) -> i32;
    
    pub fn calculator_subscribe_calculation_performed(
        handle: *mut CalculatorClientHandle,
        callback: CalculationPerformedCallback,
        user_data: *mut c_void,
    );
    
    pub fn calculator_subscribe_error_occurred(
        handle: *mut CalculatorClientHandle,
        callback: ErrorOccurredCallback,
        user_data: *mut c_void,
    );
    
    pub fn calculator_subscribe_attribute_changed(
        handle: *mut CalculatorClientHandle,
        callback: AttributeChangedCallback,
        user_data: *mut c_void,
    );
}
```

### Rust Safe Wrapper (calculator.rs)

```rust
mod calculator_ffi;

use calculator_ffi::*;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::ptr;
use std::sync::Arc;
use std::time::Duration;

pub type Result<T> = std::result::Result<T, CalculatorError>;

pub struct CalculatorClient {
    handle: *mut CalculatorClientHandle,
}

impl CalculatorClient {
    /// Create a new calculator client
    pub fn new() -> Option<Self> {
        let handle = unsafe { calculator_client_create() };
        
        if handle.is_null() {
            None
        } else {
            Some(CalculatorClient { handle })
        }
    }
    
    /// Wait for service to become available
    pub fn wait_available(&self, timeout: Duration) -> bool {
        unsafe {
            calculator_client_wait_available(
                self.handle,
                timeout.as_millis() as u32,
            )
        }
    }
    
    /// Synchronous addition
    pub fn add(&self, a: i32, b: i32) -> Result<i32> {
        let mut error = CalculatorError::None;
        let result = unsafe {
            calculator_add(self.handle, a, b, &mut error as *mut _)
        };
        
        if error == CalculatorError::None {
            Ok(result)
        } else {
            Err(error)
        }
    }
    
    /// Synchronous division with error handling
    pub fn divide(&self, dividend: i32, divisor: i32) -> Result<i32> {
        let mut error = CalculatorError::None;
        let result = unsafe {
            calculator_divide(self.handle, dividend, divisor, &mut error as *mut _)
        };
        
        match error {
            CalculatorError::None => Ok(result),
            _ => Err(error),
        }
    }
    
    /// Asynchronous addition
    pub fn add_async<F>(&self, a: i32, b: i32, callback: F)
    where
        F: FnOnce(Result<i32>) + Send + 'static,
    {
        // Box the callback to pass through FFI
        let callback_ptr = Box::into_raw(Box::new(callback));
        
        unsafe {
            calculator_add_async(
                self.handle,
                a,
                b,
                Self::add_async_trampoline::<F>,
                callback_ptr as *mut c_void,
            );
        }
    }
    
    extern "C" fn add_async_trampoline<F>(
        result: i32,
        error: CalculatorError,
        user_data: *mut c_void,
    ) where
        F: FnOnce(Result<i32>) + Send + 'static,
    {
        unsafe {
            let callback = Box::from_raw(user_data as *mut F);
            
            let res = if error == CalculatorError::None {
                Ok(result)
            } else {
                Err(error)
            };
            
            callback(res);
        }
    }
    
    /// Get last result attribute
    pub fn get_last_result(&self) -> Result<i32> {
        let mut error = CalculatorError::None;
        let result = unsafe {
            calculator_get_last_result(self.handle, &mut error as *mut _)
        };
        
        if error == CalculatorError::None {
            Ok(result)
        } else {
            Err(error)
        }
    }
    
    /// Subscribe to calculation performed events
    pub fn subscribe_calculation_performed<F>(&self, callback: F)
    where
        F: Fn(&str, i32) + Send + 'static,
    {
        let callback_ptr = Box::into_raw(Box::new(callback));
        
        unsafe {
            calculator_subscribe_calculation_performed(
                self.handle,
                Self::calculation_performed_trampoline::<F>,
                callback_ptr as *mut c_void,
            );
        }
    }
    
    extern "C" fn calculation_performed_trampoline<F>(
        operation: *const c_char,
        result: i32,
        user_data: *mut c_void,
    ) where
        F: Fn(&str, i32) + Send + 'static,
    {
        unsafe {
            let callback = &*(user_data as *const F);
            let operation_str = CStr::from_ptr(operation)
                .to_str()
                .unwrap_or("");
            
            callback(operation_str, result);
        }
    }
    
    /// Subscribe to error events
    pub fn subscribe_error_occurred<F>(&self, callback: F)
    where
        F: Fn(&str) + Send + 'static,
    {
        let callback_ptr = Box::into_raw(Box::new(callback));
        
        unsafe {
            calculator_subscribe_error_occurred(
                self.handle,
                Self::error_occurred_trampoline::<F>,
                callback_ptr as *mut c_void,
            );
        }
    }
    
    extern "C" fn error_occurred_trampoline<F>(
        error_message: *const c_char,
        user_data: *mut c_void,
    ) where
        F: Fn(&str) + Send + 'static,
    {
        unsafe {
            let callback = &*(user_data as *const F);
            let message = CStr::from_ptr(error_message)
                .to_str()
                .unwrap_or("");
            
            callback(message);
        }
    }
    
    /// Subscribe to attribute changes
    pub fn subscribe_attribute_changed<F>(&self, callback: F)
    where
        F: Fn(i32) + Send + 'static,
    {
        let callback_ptr = Box::into_raw(Box::new(callback));
        
        unsafe {
            calculator_subscribe_attribute_changed(
                self.handle,
                Self::attribute_changed_trampoline::<F>,
                callback_ptr as *mut c_void,
            );
        }
    }
    
    extern "C" fn attribute_changed_trampoline<F>(
        new_value: i32,
        user_data: *mut c_void,
    ) where
        F: Fn(i32) + Send + 'static,
    {
        unsafe {
            let callback = &*(user_data as *const F);
            callback(new_value);
        }
    }
}

impl Drop for CalculatorClient {
    fn drop(&mut self) {
        unsafe {
            calculator_client_destroy(self.handle);
        }
    }
}

// Implement Send and Sync if the underlying C library is thread-safe
unsafe impl Send for CalculatorClient {}
unsafe impl Sync for CalculatorClient {}
```

### Rust Client Example (main.rs)

```rust
mod calculator;

use calculator::CalculatorClient;
use std::thread;
use std::time::Duration;

fn main() {
    println!("Starting Rust Calculator Client...");
    
    // Create client
    let client = CalculatorClient::new()
        .expect("Failed to create calculator client");
    
    // Wait for service
    println!("Waiting for service...");
    if !client.wait_available(Duration::from_secs(5)) {
        eprintln!("Service not available");
        return;
    }
    
    println!("Service is available!");
    
    // Subscribe to events
    client.subscribe_calculation_performed(|operation, result| {
        println!("[Event] Calculation: {} = {}", operation, result);
    });
    
    client.subscribe_error_occurred(|error_message| {
        println!("[Event] Error: {}", error_message);
    });
    
    client.subscribe_attribute_changed(|new_value| {
        println!("[Attribute] Last result changed to: {}", new_value);
    });
    
    // Give subscriptions time to establish
    thread::sleep(Duration::from_millis(500));
    
    // Synchronous operations
    println!("\n=== Synchronous Operations ===");
    
    match client.add(10, 20) {
        Ok(result) => println!("10 + 20 = {}", result),
        Err(e) => println!("Addition failed: {:?}", e),
    }
    
    match client.divide(100, 4) {
        Ok(result) => println!("100 / 4 = {}", result),
        Err(e) => println!("Division failed: {:?}", e),
    }
    
    match client.divide(50, 0) {
        Ok(result) => println!("50 / 0 = {}", result),
        Err(e) => println!("Division by zero detected: {:?}", e),
    }
    
    // Asynchronous operations
    println!("\n=== Asynchronous Operations ===");
    
    client.add_async(5, 15, |result| {
        match result {
            Ok(val) => println!("[Async] 5 + 15 = {}", val),
            Err(e) => println!("[Async] Failed: {:?}", e),
        }
    });
    
    client.add_async(33, 67, |result| {
        match result {
            Ok(val) => println!("[Async] 33 + 67 = {}", val),
            Err(e) => println!("[Async] Failed: {:?}", e),
        }
    });
    
    // Get attribute
    println!("\n=== Attribute Access ===");
    match client.get_last_result() {
        Ok(value) => println!("Last result: {}", value),
        Err(e) => println!("Failed to get attribute: {:?}", e),
    }
    
    // Wait for async callbacks and events
    println!("\nWaiting for events...");
    thread::sleep(Duration::from_secs(3));
    
    println!("\nRust client terminated");
}
```

### Rust Build Configuration (Cargo.toml)

```toml
[package]
name = "calculator-rust-client"
version = "0.1.0"
edition = "2021"

[dependencies]

[build-dependencies]
cc = "1.0"

[lib]
name = "calculator"
path = "src/calculator.rs"

[[bin]]
name = "calculator-client"
path = "src/main.rs"
```

### Build Script (build.rs)

```rust
// build.rs
use std::env;

fn main() {
    // Link against the C wrapper library
    println!("cargo:rustc-link-search=/path/to/build");
    println!("cargo:rustc-link-lib=calculator_c_wrapper");
    
    // Link against CommonAPI libraries
    println!("cargo:rustc-link-lib=CommonAPI");
    println!("cargo:rustc-link-lib=CommonAPI-DBus");
    
    // Tell cargo to rerun if wrapper changes
    println!("cargo:rerun-if-changed=src/calculator_ffi.rs");
}
```

---

## Advanced Features

### 1. Managed Interfaces

Managed interfaces allow a service to dynamically create and destroy interface instances at runtime.

```cpp
// Managed stub implementation
class DeviceManagerStubImpl : public DeviceManagerStubDefault {
public:
    void createDevice(const std::string& deviceId) {
        // Create a new device interface instance
        auto deviceStub = std::make_shared<DeviceStubImpl>(deviceId);
        
        // Register it as a managed instance
        bool success = registerManagedStubDevice(deviceStub, deviceId);
        
        if (success) {
            devices_[deviceId] = deviceStub;
            std::cout << "Device " << deviceId << " created" << std::endl;
        }
    }
    
    void removeDevice(const std::string& deviceId) {
        // Deregister the managed instance
        bool success = deregisterManagedStubDevice(deviceId);
        
        if (success) {
            devices_.erase(deviceId);
            std::cout << "Device " << deviceId << " removed" << std::endl;
        }
    }

private:
    std::map<std::string, std::shared_ptr<DeviceStubImpl>> devices_;
};
```

### 2. Struct Types

CommonAPI supports complex data types defined in FIDL:

```fidl
struct Point {
    Double x
    Double y
}

struct Rectangle {
    Point topLeft
    Point bottomRight
    String color
}

method calculateArea {
    in {
        Rectangle rect
    }
    out {
        Double area
    }
}
```

**Usage in C++:**

```cpp
// Creating and using structs
Calculator::Point p1{10.5, 20.3};
Calculator::Point p2{50.7, 80.9};
Calculator::Rectangle rect{p1, p2, "blue"};

// Method call
CommonAPI::CallStatus status;
double area;
proxy->calculateArea(rect, status, area);
```

### 3. Arrays and Maps

```fidl
array Int32Array of Int32

map StringToIntMap { String to Int32 }

method processData {
    in {
        Int32Array numbers
        StringToIntMap properties
    }
    out {
        Int32 sum
    }
}
```

**Usage:**

```cpp
std::vector<int32_t> numbers = {1, 2, 3, 4, 5};
std::unordered_map<std::string, int32_t> props = {
    {"count", 5},
    {"multiplier", 2}
};

int32_t sum;
proxy->processData(numbers, props, callStatus, sum);
```

### 4. Unions (Variant Types)

```fidl
union PaymentMethod {
    CreditCard card
    BankAccount account
    String cryptoWallet
}

struct CreditCard {
    String number
    String cvv
    String expiry
}

struct BankAccount {
    String accountNumber
    String routingNumber
}
```

**Usage:**

```cpp
Calculator::CreditCard card{"1234-5678-9012-3456", "123", "12/25"};
Calculator::PaymentMethod payment;
payment = card;  // Assign credit card variant

// Check which variant is set
if (payment.isType<Calculator::CreditCard>()) {
    const auto& cardData = payment.get<Calculator::CreditCard>();
    std::cout << "Card: " << cardData.getNumber() << std::endl;
}
```

### 5. Attribute Notifications

Attributes can be configured to send change notifications:

```cpp
// Stub side - fire attribute change
void CalculatorStubImpl::updateTemperature(float newTemp) {
    temperature_ = newTemp;
    
    // This triggers attribute change notifications to all subscribers
    fireTemperatureAttributeChanged(newTemp);
}

// Proxy side - subscribe to changes
proxy->getTemperatureAttribute().getChangedEvent().subscribe(
    [](const float& newValue) {
        std::cout << "Temperature changed to: " << newValue << std::endl;
    }
);
```

### 6. Method Timeout Configuration

```cpp
// Set timeout for specific method
CommonAPI::CallInfo callInfo(5000);  // 5 second timeout

proxy->add(10, 20, &callInfo, callStatus, result);

if (callStatus == CommonAPI::CallStatus::REMOTE_ERROR) {
    std::cout << "Method call timed out" << std::endl;
}
```

### 7. Selective Broadcasts

Selective broadcasts allow filtering which clients receive events:

```cpp
// Stub side - send to specific clients
void notifyPremiumUsers(const std::string& message) {
    // Filter function - only notify premium users
    auto filter = [this](const std::shared_ptr<CommonAPI::ClientId>& clientId) {
        return isPremiumUser(clientId);
    };
    
    fireMessageSelectiveEvent(message, filter);
}

// Proxy side - subscribe (same as regular broadcast)
proxy->getMessageSelectiveEvent().subscribe(
    [](const std::string& msg) {
        std::cout << "Premium message: " << msg << std::endl;
    }
);
```

---

## Best Practices

### 1. Thread Safety

**Always use the mainloop thread for CommonAPI operations:**

```cpp
class ServiceManager {
public:
    ServiceManager() {
        runtime_ = CommonAPI::Runtime::get();
        
        // Create a mainloop context
        mainloopContext_ = std::make_shared<CommonAPI::MainLoopContext>();
    }
    
    void run() {
        // Run the mainloop (blocks)
        while (true) {
            mainloopContext_->runVerifications();
            mainloopContext_->dispatch(100);  // 100ms timeout
        }
    }
    
    void callMethodFromWorkerThread(int32_t a, int32_t b) {
        // Queue the call to be executed in the mainloop thread
        mainloopContext_->schedule([this, a, b]() {
            proxy_->add(a, b,
                [](const CommonAPI::CallStatus& status, int32_t result) {
                    // Callback executed in mainloop thread
                    handleResult(result);
                });
        });
    }

private:
    std::shared_ptr<CommonAPI::Runtime> runtime_;
    std::shared_ptr<CommonAPI::MainLoopContext> mainloopContext_;
    std::shared_ptr<CalculatorProxy<>> proxy_;
};
```

### 2. Error Handling

**Always check call status and handle errors appropriately:**

```cpp
void robustMethodCall() {
    CommonAPI::CallStatus callStatus;
    Calculator::divideError methodError;
    int32_t result;
    
    proxy_->divide(100, 0, callStatus, methodError, result);
    
    // Check transport-level errors
    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
        switch (callStatus) {
            case CommonAPI::CallStatus::NOT_AVAILABLE:
                std::cerr << "Service not available" << std::endl;
                break;
            case CommonAPI::CallStatus::CONNECTION_FAILED:
                std::cerr << "Connection failed" << std::endl;
                break;
            case CommonAPI::CallStatus::REMOTE_ERROR:
                std::cerr << "Remote error or timeout" << std::endl;
                break;
            default:
                std::cerr << "Unknown error" << std::endl;
        }
        return;
    }
    
    // Check application-level errors
    if (methodError != Calculator::divideError::NO_ERROR) {
        switch (methodError) {
            case Calculator::divideError::DIVISION_BY_ZERO:
                std::cerr << "Division by zero" << std::endl;
                break;
            case Calculator::divideError::OVERFLOW:
                std::cerr << "Integer overflow" << std::endl;
                break;
        }
        return;
    }
    
    // Success - use result
    std::cout << "Result: " << result << std::endl;
}
```

### 3. Resource Management

**Use RAII and smart pointers:**

```cpp
class ServiceController {
public:
    ServiceController(const std::string& domain, 
                     const std::string& instance) 
        : domain_(domain), instance_(instance) {
        
        runtime_ = CommonAPI::Runtime::get();
        stub_ = std::make_shared<CalculatorStubImpl>();
        
        if (!runtime_->registerService(domain_, instance_, stub_)) {
            throw std::runtime_error("Failed to register service");
        }
    }
    
    ~ServiceController() {
        // Automatic cleanup - service unregistered when object destroyed
        runtime_->unregisterService(domain_, 
                                   CalculatorStubDefault::StubInterface::getInterface(),
                                   instance_);
    }
    
    // Prevent copying
    ServiceController(const ServiceController&) = delete;
    ServiceController& operator=(const ServiceController&) = delete;

private:
    std::string domain_;
    std::string instance_;
    std::shared_ptr<CommonAPI::Runtime> runtime_;
    std::shared_ptr<CalculatorStubImpl> stub_;
};
```

### 4. Logging and Debugging

**Enable CommonAPI logging:**

```cpp
// Set log level
CommonAPI::Logger::getInstance().setLogLevel(CommonAPI::Logger::Level::LL_INFO);

// Custom log handler
class CustomLogger : public CommonAPI::Logger {
public:
    void doLog(Level level, const std::string& message) override {
        std::cout << "[" << getLevelString(level) << "] " 
                  << message << std::endl;
    }
};

// Use custom logger
CommonAPI::Logger::getInstance().setLogger(
    std::make_shared<CustomLogger>()
);
```

### 5. Configuration Management

**Organize configuration files properly:**

```
project/
├── config/
│   ├── commonapi.ini           # Main config
│   ├── commonapi-dbus.ini      # D-Bus specific
│   └── commonapi-someip.json   # SOME/IP specific
├── fidl/
│   └── Calculator.fidl
├── src-gen/                     # Generated code
└── src/                         # Your implementation
```

**commonapi.ini:**
```ini
[logging]
level = info
console = true

[default]
binding = dbus

[timeout]
default = 5000

[threads]
dispatcherThreads = 2
```

### 6. Version Management

**Handle multiple interface versions:**

```fidl
// Version 1.0
package org.example

interface Calculator {
    version { major 1 minor 0 }
    // ... methods
}

// Version 2.0 - new file
package org.example

interface Calculator {
    version { major 2 minor 0 }
    // ... methods (including new ones)
}
```

**Client code:**

```cpp
// Support multiple versions
auto proxyV1 = runtime->buildProxy<v1::org::example::CalculatorProxy>(
    domain, instance);

auto proxyV2 = runtime->buildProxy<v2::org::example::CalculatorProxy>(
    domain, instance);

// Check which version is available
if (proxyV2->isAvailable()) {
    // Use v2 features
} else if (proxyV1->isAvailable()) {
    // Fall back to v1
}
```

---

## Real-World Use Cases

### 1. Automotive Head Unit System

**Scenario:** Media player service communicating with UI application

```fidl
package com.automotive.media

interface MediaPlayer {
    version { major 1 minor 0 }
    
    attribute PlaybackState state
    attribute Track currentTrack
    attribute UInt32 volume
    
    method play {}
    method pause {}
    method stop {}
    method next {}
    method previous {}
    method setVolume { in { UInt32 level } }
    
    broadcast trackChanged {
        out { Track track }
    }
    
    broadcast playbackStateChanged {
        out { PlaybackState state }
    }
}

enumeration PlaybackState {
    STOPPED = 0
    PLAYING = 1
    PAUSED = 2
}

struct Track {
    String title
    String artist
    String album
    UInt32 duration
}
```

### 2. Industrial IoT Sensor Network

**Scenario:** Sensor hub aggregating data from multiple sensors

```fidl
package com.industrial.sensors

interface SensorHub {
    version { major 1 minor 0 }
    
    // Managed interface for individual sensors
    managedInterface Sensor {
        attribute Float32 value readonly
        attribute String unit readonly
        attribute UInt64 timestamp readonly
        
        method calibrate {
            in { Float32 offset }
        }
        
        broadcast valueChanged {
            out {
                Float32 value
                UInt64 timestamp
            }
        }
    }
    
    method getSensorList {
        out { StringArray sensorIds }
    }
    
    method createSensor {
        in {
            String sensorId
            String type
        }
    }
}
```

### 3. Smart Home Automation

**Scenario:** Central controller managing various smart devices

```fidl
package com.smarthome

interface DeviceController {
    version { major 1 minor 0 }
    
    union DeviceCommand {
        LightCommand light
        ThermostatCommand thermostat
        LockCommand lock
    }
    
    struct LightCommand {
        Boolean on
        UInt8 brightness
        String color
    }
    
    struct ThermostatCommand {
        Float32 temperature
        String mode
    }
    
    struct LockCommand {
        Boolean locked
    }
    
    method sendCommand {
        in {
            String deviceId
            DeviceCommand command
        }
        out {
            Boolean success
        }
    }
    
    broadcast deviceStatusChanged selective {
        out {
            String deviceId
            String status
        }
    }
}
```

---

## Conclusion

CommonAPI provides a powerful, protocol-agnostic framework for inter-process communication in embedded and automotive systems. Key takeaways:

1. **Abstraction**: Write once, deploy on multiple IPC backends
2. **Type Safety**: Strong typing with automatic serialization
3. **Asynchronous Support**: Non-blocking operations with callbacks
4. **Code Generation**: Minimize boilerplate with FIDL-based code generation
5. **Multi-Language Support**: While primarily C++, interfacing with C and Rust is achievable through FFI

The framework is particularly well-suited for:
- **Automotive systems** (AUTOSAR Adaptive Platform)
- **Embedded Linux** applications
- **Industrial control** systems
- **IoT gateways** and edge devices

By following the patterns and best practices outlined in this guide, developers can build robust, maintainable IPC-based applications that scale from simple client-server architectures to complex distributed systems.

---

## Additional Resources

- **Official CommonAPI Documentation**: https://covesa.github.io/capicxx-core-tools/
- **Franca IDL Reference**: https://github.com/franca/franca
- **GENIVI Alliance**: https://www.covesa.global/
- **AUTOSAR Adaptive Platform**: https://www.autosar.org/

---

