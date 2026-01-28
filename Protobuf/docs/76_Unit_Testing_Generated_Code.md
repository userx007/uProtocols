# Unit Testing Generated Protobuf Code

## Overview

Unit testing Protocol Buffer (protobuf) generated code is essential for ensuring data serialization, deserialization, and message handling work correctly across your application. This guide covers testing strategies, mocking techniques, and best practices for C/C++ and Rust implementations.

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [Testing Strategies](#testing-strategies)
3. [C++ Implementation](#cpp-implementation)
4. [C Implementation](#c-implementation)
5. [Rust Implementation](#rust-implementation)
6. [Mocking and Test Doubles](#mocking-and-test-doubles)
7. [Best Practices](#best-practices)

---

## Core Concepts

### What to Test

When testing protobuf-based code, focus on:

- **Serialization/Deserialization**: Verify that messages can be encoded and decoded correctly
- **Field Access**: Ensure getters and setters work as expected
- **Default Values**: Confirm proper handling of default and missing fields
- **Validation**: Test custom validation logic
- **Wire Format Compatibility**: Verify backward/forward compatibility
- **Edge Cases**: Test with empty messages, maximum values, and special characters

### Testing Layers

1. **Unit Tests**: Test individual message operations
2. **Integration Tests**: Test message passing between components
3. **Property-Based Tests**: Generate random valid messages for fuzzing
4. **Compatibility Tests**: Verify wire format compatibility across versions

---

## Testing Strategies

### 1. Golden File Testing

Compare serialized output against known-good binary files:

```
tests/
  golden/
    user_message_v1.bin
    user_message_v2.bin
  test_compatibility.cpp
```

### 2. Round-Trip Testing

Serialize and deserialize messages to verify data integrity:

```
Original Message -> Serialize -> Binary Data -> Deserialize -> Reconstructed Message
```

### 3. Equivalence Testing

Compare messages using protobuf's built-in equality:

```cpp
ASSERT_TRUE(MessageDifferencer::Equals(msg1, msg2));
```

### 4. Text Format Testing

Use human-readable text format for easier debugging:

```protobuf
user {
  id: 123
  name: "Alice"
  email: "alice@example.com"
}
```

---

## C++ Implementation

### Example Proto Definition

```protobuf
// user.proto
syntax = "proto3";

package example;

message User {
  int32 id = 1;
  string name = 2;
  string email = 3;
  repeated string tags = 4;
  
  message Address {
    string street = 1;
    string city = 2;
    string country = 3;
  }
  
  Address address = 5;
}

message UserList {
  repeated User users = 1;
}
```

### Basic Unit Tests with Google Test

```cpp
// test_user.cpp
#include <gtest/gtest.h>
#include "user.pb.h"
#include <google/protobuf/util/message_differencer.h>

using namespace example;
using google::protobuf::util::MessageDifferencer;

class UserTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup runs before each test
        user_.set_id(123);
        user_.set_name("Alice");
        user_.set_email("alice@example.com");
    }
    
    void TearDown() override {
        // Cleanup runs after each test
    }
    
    User user_;
};

// Test basic field access
TEST_F(UserTest, BasicFieldAccess) {
    EXPECT_EQ(user_.id(), 123);
    EXPECT_EQ(user_.name(), "Alice");
    EXPECT_EQ(user_.email(), "alice@example.com");
}

// Test serialization and deserialization
TEST_F(UserTest, SerializationRoundTrip) {
    // Serialize
    std::string serialized;
    ASSERT_TRUE(user_.SerializeToString(&serialized));
    EXPECT_FALSE(serialized.empty());
    
    // Deserialize
    User deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    
    // Verify equality
    EXPECT_TRUE(MessageDifferencer::Equals(user_, deserialized));
}

// Test default values
TEST_F(UserTest, DefaultValues) {
    User empty_user;
    EXPECT_EQ(empty_user.id(), 0);
    EXPECT_EQ(empty_user.name(), "");
    EXPECT_EQ(empty_user.email(), "");
    EXPECT_EQ(empty_user.tags_size(), 0);
}

// Test repeated fields
TEST_F(UserTest, RepeatedFields) {
    user_.add_tags("developer");
    user_.add_tags("golang");
    user_.add_tags("protobuf");
    
    EXPECT_EQ(user_.tags_size(), 3);
    EXPECT_EQ(user_.tags(0), "developer");
    EXPECT_EQ(user_.tags(1), "golang");
    EXPECT_EQ(user_.tags(2), "protobuf");
    
    // Test iteration
    std::vector<std::string> expected = {"developer", "golang", "protobuf"};
    std::vector<std::string> actual(user_.tags().begin(), user_.tags().end());
    EXPECT_EQ(actual, expected);
}

// Test nested messages
TEST_F(UserTest, NestedMessages) {
    auto* address = user_.mutable_address();
    address->set_street("123 Main St");
    address->set_city("San Francisco");
    address->set_country("USA");
    
    EXPECT_TRUE(user_.has_address());
    EXPECT_EQ(user_.address().street(), "123 Main St");
    EXPECT_EQ(user_.address().city(), "San Francisco");
    EXPECT_EQ(user_.address().country(), "USA");
}

// Test message clearing
TEST_F(UserTest, ClearMessage) {
    EXPECT_FALSE(user_.name().empty());
    user_.Clear();
    EXPECT_TRUE(user_.name().empty());
    EXPECT_EQ(user_.id(), 0);
}

// Test text format
TEST_F(UserTest, TextFormat) {
    std::string text_format;
    google::protobuf::TextFormat::PrintToString(user_, &text_format);
    
    User parsed;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(text_format, &parsed));
    EXPECT_TRUE(MessageDifferencer::Equals(user_, parsed));
}

// Test JSON format (proto3)
TEST_F(UserTest, JsonFormat) {
    std::string json;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    
    auto status = google::protobuf::util::MessageToJsonString(user_, &json, options);
    ASSERT_TRUE(status.ok());
    
    User parsed;
    status = google::protobuf::util::JsonStringToMessage(json, &parsed);
    ASSERT_TRUE(status.ok());
    EXPECT_TRUE(MessageDifferencer::Equals(user_, parsed));
}
```

### Testing Wire Format Compatibility

```cpp
// test_compatibility.cpp
#include <gtest/gtest.h>
#include <fstream>
#include "user.pb.h"

class CompatibilityTest : public ::testing::Test {
protected:
    bool LoadGoldenFile(const std::string& filename, std::string* data) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;
        
        *data = std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );
        return true;
    }
    
    bool SaveGoldenFile(const std::string& filename, const std::string& data) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) return false;
        
        file.write(data.data(), data.size());
        return true;
    }
};

TEST_F(CompatibilityTest, LoadV1Format) {
    std::string golden_data;
    ASSERT_TRUE(LoadGoldenFile("testdata/user_v1.bin", &golden_data));
    
    User user;
    ASSERT_TRUE(user.ParseFromString(golden_data));
    
    // Verify expected fields are present
    EXPECT_GT(user.id(), 0);
    EXPECT_FALSE(user.name().empty());
}

TEST_F(CompatibilityTest, BackwardCompatibility) {
    // Create a message with new fields
    User user;
    user.set_id(456);
    user.set_name("Bob");
    user.set_email("bob@example.com");
    user.add_tags("engineer");
    
    std::string serialized;
    ASSERT_TRUE(user.SerializeToString(&serialized));
    
    // Old code (without tags field) should still parse successfully
    User old_format;
    ASSERT_TRUE(old_format.ParseFromString(serialized));
    EXPECT_EQ(old_format.id(), 456);
    EXPECT_EQ(old_format.name(), "Bob");
}
```

### Mocking Protobuf Messages

```cpp
// mock_user_service.h
#include <gmock/gmock.h>
#include "user.pb.h"

class UserServiceInterface {
public:
    virtual ~UserServiceInterface() = default;
    virtual bool SaveUser(const User& user) = 0;
    virtual bool LoadUser(int32_t id, User* user) = 0;
};

class MockUserService : public UserServiceInterface {
public:
    MOCK_METHOD(bool, SaveUser, (const User& user), (override));
    MOCK_METHOD(bool, LoadUser, (int32_t id, User* user), (override));
};

// test_with_mock.cpp
#include <gtest/gtest.h>
#include "mock_user_service.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::DoAll;

TEST(UserServiceTest, SaveUserSuccess) {
    MockUserService mock_service;
    
    User user;
    user.set_id(123);
    user.set_name("Alice");
    
    EXPECT_CALL(mock_service, SaveUser(_))
        .Times(1)
        .WillOnce(Return(true));
    
    EXPECT_TRUE(mock_service.SaveUser(user));
}

TEST(UserServiceTest, LoadUserSuccess) {
    MockUserService mock_service;
    
    User expected_user;
    expected_user.set_id(123);
    expected_user.set_name("Alice");
    
    EXPECT_CALL(mock_service, LoadUser(123, _))
        .Times(1)
        .WillOnce(DoAll(
            SetArgPointee<1>(expected_user),
            Return(true)
        ));
    
    User loaded_user;
    EXPECT_TRUE(mock_service.LoadUser(123, &loaded_user));
    EXPECT_EQ(loaded_user.id(), 123);
    EXPECT_EQ(loaded_user.name(), "Alice");
}
```

### Custom Matchers for Protobuf

```cpp
// protobuf_matchers.h
#include <gmock/gmock.h>
#include <google/protobuf/util/message_differencer.h>

MATCHER_P(EqualsProto, expected, "") {
    return google::protobuf::util::MessageDifferencer::Equals(arg, expected);
}

MATCHER_P(HasField, field_name, "") {
    const google::protobuf::Descriptor* descriptor = arg.GetDescriptor();
    const google::protobuf::Reflection* reflection = arg.GetReflection();
    const google::protobuf::FieldDescriptor* field = 
        descriptor->FindFieldByName(field_name);
    
    if (!field) {
        *result_listener << "field " << field_name << " not found";
        return false;
    }
    
    return reflection->HasField(arg, field);
}

// Usage
TEST(MatcherTest, UseCustomMatchers) {
    User user1;
    user1.set_id(123);
    user1.set_name("Alice");
    
    User user2;
    user2.set_id(123);
    user2.set_name("Alice");
    
    EXPECT_THAT(user1, EqualsProto(user2));
    EXPECT_THAT(user1, HasField("name"));
}
```

---

## C Implementation

For C, we'll use the protobuf-c library:

### CMakeLists.txt Setup

```cmake
find_package(protobuf-c REQUIRED)
find_package(Check REQUIRED)

add_executable(test_user_c
    test_user.c
    user.pb-c.c
)

target_link_libraries(test_user_c
    protobuf-c::protobuf-c
    Check::check
)
```

### Unit Tests with Check Framework

```c
// test_user.c
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "user.pb-c.h"

// Test fixture setup
Example__User* create_test_user(void) {
    Example__User* user = malloc(sizeof(Example__User));
    example__user__init(user);
    
    user->id = 123;
    user->name = strdup("Alice");
    user->email = strdup("alice@example.com");
    
    return user;
}

void free_test_user(Example__User* user) {
    if (user->name) free(user->name);
    if (user->email) free(user->email);
    free(user);
}

// Basic field access test
START_TEST(test_basic_field_access) {
    Example__User* user = create_test_user();
    
    ck_assert_int_eq(user->id, 123);
    ck_assert_str_eq(user->name, "Alice");
    ck_assert_str_eq(user->email, "alice@example.com");
    
    free_test_user(user);
}
END_TEST

// Serialization round-trip test
START_TEST(test_serialization_roundtrip) {
    Example__User* user = create_test_user();
    
    // Get packed size
    size_t packed_size = example__user__get_packed_size(user);
    ck_assert_int_gt(packed_size, 0);
    
    // Allocate buffer
    uint8_t* buffer = malloc(packed_size);
    ck_assert_ptr_nonnull(buffer);
    
    // Pack
    size_t written = example__user__pack(user, buffer);
    ck_assert_int_eq(written, packed_size);
    
    // Unpack
    Example__User* unpacked = example__user__unpack(NULL, packed_size, buffer);
    ck_assert_ptr_nonnull(unpacked);
    
    // Verify
    ck_assert_int_eq(unpacked->id, user->id);
    ck_assert_str_eq(unpacked->name, user->name);
    ck_assert_str_eq(unpacked->email, user->email);
    
    // Cleanup
    example__user__free_unpacked(unpacked, NULL);
    free(buffer);
    free_test_user(user);
}
END_TEST

// Test repeated fields
START_TEST(test_repeated_fields) {
    Example__User* user = create_test_user();
    
    // Add tags
    user->n_tags = 3;
    user->tags = malloc(sizeof(char*) * 3);
    user->tags[0] = strdup("developer");
    user->tags[1] = strdup("golang");
    user->tags[2] = strdup("protobuf");
    
    // Verify
    ck_assert_int_eq(user->n_tags, 3);
    ck_assert_str_eq(user->tags[0], "developer");
    ck_assert_str_eq(user->tags[1], "golang");
    ck_assert_str_eq(user->tags[2], "protobuf");
    
    // Cleanup
    for (size_t i = 0; i < user->n_tags; i++) {
        free(user->tags[i]);
    }
    free(user->tags);
    free_test_user(user);
}
END_TEST

// Test nested messages
START_TEST(test_nested_messages) {
    Example__User* user = create_test_user();
    
    // Create address
    Example__User__Address* address = malloc(sizeof(Example__User__Address));
    example__user__address__init(address);
    address->street = strdup("123 Main St");
    address->city = strdup("San Francisco");
    address->country = strdup("USA");
    
    user->address = address;
    
    // Verify
    ck_assert_ptr_nonnull(user->address);
    ck_assert_str_eq(user->address->street, "123 Main St");
    ck_assert_str_eq(user->address->city, "San Francisco");
    ck_assert_str_eq(user->address->country, "USA");
    
    // Cleanup
    free(address->street);
    free(address->city);
    free(address->country);
    free(address);
    free_test_user(user);
}
END_TEST

// Test default values
START_TEST(test_default_values) {
    Example__User user = EXAMPLE__USER__INIT;
    
    ck_assert_int_eq(user.id, 0);
    ck_assert_ptr_null(user.name);
    ck_assert_ptr_null(user.email);
    ck_assert_int_eq(user.n_tags, 0);
    ck_assert_ptr_null(user.tags);
}
END_TEST

// Test suite setup
Suite* user_suite(void) {
    Suite* s = suite_create("User");
    
    TCase* tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_basic_field_access);
    tcase_add_test(tc_core, test_serialization_roundtrip);
    tcase_add_test(tc_core, test_repeated_fields);
    tcase_add_test(tc_core, test_nested_messages);
    tcase_add_test(tc_core, test_default_values);
    
    suite_add_tcase(s, tc_core);
    
    return s;
}

int main(void) {
    int number_failed;
    Suite* s = user_suite();
    SRunner* sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

### Helper Functions for C Testing

```c
// test_helpers.h
#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdbool.h>
#include <stdint.h>

// Compare two serialized messages
bool compare_serialized(const uint8_t* data1, size_t len1,
                       const uint8_t* data2, size_t len2);

// Load golden file
bool load_golden_file(const char* filename, uint8_t** data, size_t* len);

// Save golden file
bool save_golden_file(const char* filename, const uint8_t* data, size_t len);

#endif

// test_helpers.c
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool compare_serialized(const uint8_t* data1, size_t len1,
                       const uint8_t* data2, size_t len2) {
    if (len1 != len2) return false;
    return memcmp(data1, data2, len1) == 0;
}

bool load_golden_file(const char* filename, uint8_t** data, size_t* len) {
    FILE* file = fopen(filename, "rb");
    if (!file) return false;
    
    fseek(file, 0, SEEK_END);
    *len = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    *data = malloc(*len);
    if (!*data) {
        fclose(file);
        return false;
    }
    
    size_t read = fread(*data, 1, *len, file);
    fclose(file);
    
    return read == *len;
}

bool save_golden_file(const char* filename, const uint8_t* data, size_t len) {
    FILE* file = fopen(filename, "wb");
    if (!file) return false;
    
    size_t written = fwrite(data, 1, len, file);
    fclose(file);
    
    return written == len;
}
```

---

## Rust Implementation

### Cargo.toml Dependencies

```toml
[dependencies]
prost = "0.12"
bytes = "1.5"

[dev-dependencies]
prost-build = "0.12"
mockall = "0.12"

[build-dependencies]
prost-build = "0.12"
```

### Build Script

```rust
// build.rs
fn main() {
    prost_build::compile_protos(&["src/user.proto"], &["src/"]).unwrap();
}
```

### Unit Tests with Rust's Built-in Testing

```rust
// lib.rs or tests/user_test.rs
#[cfg(test)]
mod tests {
    use prost::Message;
    
    // Include generated code
    pub mod example {
        include!(concat!(env!("OUT_DIR"), "/example.rs"));
    }
    
    use example::{User, user::Address};
    
    #[test]
    fn test_basic_field_access() {
        let user = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec![],
            address: None,
        };
        
        assert_eq!(user.id, 123);
        assert_eq!(user.name, "Alice");
        assert_eq!(user.email, "alice@example.com");
    }
    
    #[test]
    fn test_serialization_roundtrip() {
        let original = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec!["developer".to_string(), "rust".to_string()],
            address: None,
        };
        
        // Serialize
        let mut buffer = Vec::new();
        original.encode(&mut buffer).expect("Failed to encode");
        assert!(!buffer.is_empty());
        
        // Deserialize
        let decoded = User::decode(&buffer[..]).expect("Failed to decode");
        
        // Verify
        assert_eq!(original, decoded);
    }
    
    #[test]
    fn test_default_values() {
        let user = User::default();
        
        assert_eq!(user.id, 0);
        assert_eq!(user.name, "");
        assert_eq!(user.email, "");
        assert!(user.tags.is_empty());
        assert!(user.address.is_none());
    }
    
    #[test]
    fn test_repeated_fields() {
        let mut user = User::default();
        user.id = 123;
        user.tags = vec![
            "developer".to_string(),
            "rust".to_string(),
            "protobuf".to_string(),
        ];
        
        assert_eq!(user.tags.len(), 3);
        assert_eq!(user.tags[0], "developer");
        assert_eq!(user.tags[1], "rust");
        assert_eq!(user.tags[2], "protobuf");
        
        // Test iteration
        let expected = vec!["developer", "rust", "protobuf"];
        let actual: Vec<&str> = user.tags.iter().map(|s| s.as_str()).collect();
        assert_eq!(actual, expected);
    }
    
    #[test]
    fn test_nested_messages() {
        let address = Address {
            street: "123 Main St".to_string(),
            city: "San Francisco".to_string(),
            country: "USA".to_string(),
        };
        
        let user = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec![],
            address: Some(address),
        };
        
        assert!(user.address.is_some());
        let addr = user.address.as_ref().unwrap();
        assert_eq!(addr.street, "123 Main St");
        assert_eq!(addr.city, "San Francisco");
        assert_eq!(addr.country, "USA");
    }
    
    #[test]
    fn test_clone_and_equality() {
        let user1 = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec!["developer".to_string()],
            address: None,
        };
        
        let user2 = user1.clone();
        
        assert_eq!(user1, user2);
        assert_eq!(user1.id, user2.id);
        assert_eq!(user1.name, user2.name);
    }
    
    #[test]
    fn test_encoded_len() {
        let user = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec![],
            address: None,
        };
        
        let encoded_len = user.encoded_len();
        
        let mut buffer = Vec::new();
        user.encode(&mut buffer).unwrap();
        
        assert_eq!(encoded_len, buffer.len());
    }
    
    #[test]
    fn test_clear_message() {
        let mut user = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec!["developer".to_string()],
            address: None,
        };
        
        user.clear();
        
        assert_eq!(user.id, 0);
        assert_eq!(user.name, "");
        assert_eq!(user.email, "");
        assert!(user.tags.is_empty());
    }
}
```

### Property-Based Testing with Proptest

```rust
// tests/property_tests.rs
use proptest::prelude::*;
use prost::Message;

pub mod example {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

use example::User;

proptest! {
    #[test]
    fn test_roundtrip_preserves_data(
        id in any::<i32>(),
        name in "[a-zA-Z]{1,50}",
        email in "[a-z]{1,20}@[a-z]{1,10}\\.(com|org|net)",
        tags in prop::collection::vec("[a-z]{1,20}", 0..10)
    ) {
        let original = User {
            id,
            name,
            email,
            tags,
            address: None,
        };
        
        let mut buffer = Vec::new();
        original.encode(&mut buffer).unwrap();
        
        let decoded = User::decode(&buffer[..]).unwrap();
        
        prop_assert_eq!(original, decoded);
    }
    
    #[test]
    fn test_encoding_is_deterministic(
        id in any::<i32>(),
        name in "[a-zA-Z]{1,50}"
    ) {
        let user = User {
            id,
            name,
            email: String::new(),
            tags: vec![],
            address: None,
        };
        
        let mut buffer1 = Vec::new();
        let mut buffer2 = Vec::new();
        
        user.encode(&mut buffer1).unwrap();
        user.encode(&mut buffer2).unwrap();
        
        prop_assert_eq!(buffer1, buffer2);
    }
}
```

### Mocking with Mockall

```rust
// mock_service.rs
use mockall::automock;

pub mod example {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

use example::User;

#[automock]
pub trait UserService {
    fn save_user(&self, user: &User) -> Result<(), String>;
    fn load_user(&self, id: i32) -> Result<User, String>;
    fn delete_user(&self, id: i32) -> Result<(), String>;
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_save_user_success() {
        let mut mock_service = MockUserService::new();
        
        let user = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec![],
            address: None,
        };
        
        mock_service
            .expect_save_user()
            .times(1)
            .returning(|_| Ok(()));
        
        let result = mock_service.save_user(&user);
        assert!(result.is_ok());
    }
    
    #[test]
    fn test_load_user_success() {
        let mut mock_service = MockUserService::new();
        
        let expected_user = User {
            id: 123,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            tags: vec![],
            address: None,
        };
        
        let expected_clone = expected_user.clone();
        
        mock_service
            .expect_load_user()
            .with(mockall::predicate::eq(123))
            .times(1)
            .returning(move |_| Ok(expected_clone.clone()));
        
        let result = mock_service.load_user(123);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), expected_user);
    }
    
    #[test]
    fn test_load_user_not_found() {
        let mut mock_service = MockUserService::new();
        
        mock_service
            .expect_load_user()
            .with(mockall::predicate::eq(999))
            .times(1)
            .returning(|_| Err("User not found".to_string()));
        
        let result = mock_service.load_user(999);
        assert!(result.is_err());
        assert_eq!(result.unwrap_err(), "User not found");
    }
}
```

### Testing Wire Format Compatibility

```rust
// tests/compatibility_tests.rs
use std::fs;
use prost::Message;

pub mod example {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

use example::User;

#[test]
fn test_load_golden_file_v1() {
    let golden_data = fs::read("testdata/user_v1.bin")
        .expect("Failed to read golden file");
    
    let user = User::decode(&golden_data[..])
        .expect("Failed to decode golden file");
    
    assert!(user.id > 0);
    assert!(!user.name.is_empty());
}

#[test]
fn test_backward_compatibility() {
    // Create a message with new fields
    let user = User {
        id: 456,
        name: "Bob".to_string(),
        email: "bob@example.com".to_string(),
        tags: vec!["engineer".to_string()],
        address: None,
    };
    
    let mut buffer = Vec::new();
    user.encode(&mut buffer).unwrap();
    
    // Old format (without tags) should still parse
    let decoded = User::decode(&buffer[..]).unwrap();
    assert_eq!(decoded.id, 456);
    assert_eq!(decoded.name, "Bob");
    assert_eq!(decoded.email, "bob@example.com");
}

#[test]
fn test_forward_compatibility() {
    // Simulate old format (minimal fields)
    let old_user = User {
        id: 789,
        name: "Charlie".to_string(),
        email: String::new(),
        tags: vec![],
        address: None,
    };
    
    let mut buffer = Vec::new();
    old_user.encode(&mut buffer).unwrap();
    
    // New code should handle missing optional fields
    let new_user = User::decode(&buffer[..]).unwrap();
    assert_eq!(new_user.id, 789);
    assert_eq!(new_user.name, "Charlie");
    assert!(new_user.email.is_empty());
    assert!(new_user.tags.is_empty());
}
```

---

## Mocking and Test Doubles

### Strategy 1: Interface-Based Mocking

Create interfaces/traits for your protobuf-dependent components and mock those interfaces.

**C++ Example:**
```cpp
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual bool ProcessMessage(const User& user) = 0;
};

class MockMessageHandler : public IMessageHandler {
public:
    MOCK_METHOD(bool, ProcessMessage, (const User& user), (override));
};
```

**Rust Example:**
```rust
#[automock]
trait MessageHandler {
    fn process_message(&self, user: &User) -> Result<(), Error>;
}
```

### Strategy 2: Dependency Injection

Inject serialization/deserialization logic for easier testing.

```cpp
class UserRepository {
public:
    explicit UserRepository(std::function<bool(const User&)> serializer)
        : serializer_(serializer) {}
    
    bool Save(const User& user) {
        return serializer_(user);
    }
    
private:
    std::function<bool(const User&)> serializer_;
};

// In tests
TEST(UserRepositoryTest, SaveWithMock) {
    bool called = false;
    auto mock_serializer = [&called](const User&) {
        called = true;
        return true;
    };
    
    UserRepository repo(mock_serializer);
    User user;
    user.set_id(123);
    
    EXPECT_TRUE(repo.Save(user));
    EXPECT_TRUE(called);
}
```

### Strategy 3: Fake Implementations

Create lightweight fake implementations for testing.

```rust
struct FakeUserService {
    users: std::sync::Mutex<std::collections::HashMap<i32, User>>,
}

impl FakeUserService {
    fn new() -> Self {
        Self {
            users: std::sync::Mutex::new(std::collections::HashMap::new()),
        }
    }
}

impl UserService for FakeUserService {
    fn save_user(&self, user: &User) -> Result<(), String> {
        let mut users = self.users.lock().unwrap();
        users.insert(user.id, user.clone());
        Ok(())
    }
    
    fn load_user(&self, id: i32) -> Result<User, String> {
        let users = self.users.lock().unwrap();
        users.get(&id)
            .cloned()
            .ok_or_else(|| "User not found".to_string())
    }
    
    fn delete_user(&self, id: i32) -> Result<(), String> {
        let mut users = self.users.lock().unwrap();
        users.remove(&id);
        Ok(())
    }
}
```

---

## Best Practices

### 1. Test Organization

```
tests/
├── unit/
│   ├── test_user.cpp
│   ├── test_message.cpp
│   └── test_serialization.cpp
├── integration/
│   ├── test_end_to_end.cpp
│   └── test_service_integration.cpp
├── compatibility/
│   ├── test_backward_compat.cpp
│   └── test_forward_compat.cpp
└── testdata/
    ├── golden/
    │   ├── user_v1.bin
    │   └── user_v2.bin
    └── fixtures/
        ├── valid_users.json
        └── invalid_users.json
```

### 2. Use Builders for Complex Messages

```cpp
class UserBuilder {
public:
    UserBuilder& WithId(int32_t id) {
        user_.set_id(id);
        return *this;
    }
    
    UserBuilder& WithName(const std::string& name) {
        user_.set_name(name);
        return *this;
    }
    
    UserBuilder& WithEmail(const std::string& email) {
        user_.set_email(email);
        return *this;
    }
    
    UserBuilder& AddTag(const std::string& tag) {
        user_.add_tags(tag);
        return *this;
    }
    
    User Build() const {
        return user_;
    }
    
private:
    User user_;
};

// Usage in tests
TEST(BuilderTest, CreateComplexUser) {
    User user = UserBuilder()
        .WithId(123)
        .WithName("Alice")
        .WithEmail("alice@example.com")
        .AddTag("developer")
        .AddTag("golang")
        .Build();
    
    EXPECT_EQ(user.id(), 123);
    EXPECT_EQ(user.tags_size(), 2);
}
```

### 3. Test Edge Cases

```rust
#[test]
fn test_empty_message() {
    let user = User::default();
    let mut buffer = Vec::new();
    user.encode(&mut buffer).unwrap();
    
    let decoded = User::decode(&buffer[..]).unwrap();
    assert_eq!(decoded, user);
}

#[test]
fn test_maximum_values() {
    let user = User {
        id: i32::MAX,
        name: "A".repeat(10000),
        email: "test@example.com".to_string(),
        tags: vec!["tag".to_string(); 1000],
        address: None,
    };
    
    let mut buffer = Vec::new();
    user.encode(&mut buffer).unwrap();
    
    let decoded = User::decode(&buffer[..]).unwrap();
    assert_eq!(decoded.id, i32::MAX);
    assert_eq!(decoded.name.len(), 10000);
    assert_eq!(decoded.tags.len(), 1000);
}

#[test]
fn test_special_characters() {
    let user = User {
        id: 1,
        name: "用户名 🦀".to_string(),
        email: "user@测试.com".to_string(),
        tags: vec![],
        address: None,
    };
    
    let mut buffer = Vec::new();
    user.encode(&mut buffer).unwrap();
    
    let decoded = User::decode(&buffer[..]).unwrap();
    assert_eq!(decoded.name, "用户名 🦀");
    assert_eq!(decoded.email, "user@测试.com");
}
```

### 4. Performance Testing

```cpp
TEST(PerformanceTest, SerializationSpeed) {
    User user;
    user.set_id(123);
    user.set_name("Alice");
    user.set_email("alice@example.com");
    
    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        std::string serialized;
        user.SerializeToString(&serialized);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Average serialization time: " 
              << duration.count() / iterations << " μs" << std::endl;
    
    EXPECT_LT(duration.count() / iterations, 100); // Less than 100μs per operation
}
```

### 5. Continuous Integration Setup

```yaml
# .github/workflows/test.yml
name: Protobuf Tests

on: [push, pull_request]

jobs:
  test-cpp:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y protobuf-compiler libprotobuf-dev
          sudo apt-get install -y libgtest-dev libgmock-dev
      - name: Build and test
        run: |
          mkdir build && cd build
          cmake ..
          make
          ctest --output-on-failure
  
  test-rust:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - uses: actions-rs/toolchain@v1
        with:
          toolchain: stable
      - name: Install protoc
        run: |
          sudo apt-get update
          sudo apt-get install -y protobuf-compiler
      - name: Run tests
        run: cargo test --all-features
```

### 6. Code Coverage

```bash
# C++ with gcov/lcov
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage" ..
make
ctest
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report

# Rust with tarpaulin
cargo install cargo-tarpaulin
cargo tarpaulin --out Html --output-dir coverage
```

---

## Summary

### Key Takeaways

1. **Comprehensive Testing Layers**
   - Unit tests for individual message operations
   - Integration tests for message passing between components
   - Property-based tests for fuzzing and edge case discovery
   - Compatibility tests for wire format versioning

2. **Language-Specific Approaches**
   - **C++**: Use Google Test/Mock with protobuf's built-in utilities
   - **C**: Use Check framework with manual memory management
   - **Rust**: Leverage built-in testing, proptest, and mockall

3. **Essential Testing Patterns**
   - Round-trip serialization testing
   - Golden file comparison for backward compatibility
   - Builder patterns for complex message construction
   - Interface-based mocking for dependency isolation

4. **Critical Test Areas**
   - Serialization/deserialization correctness
   - Default value handling
   - Repeated and nested field operations
   - Wire format compatibility across versions
   - Edge cases (empty messages, maximum values, special characters)

5. **Best Practices**
   - Always test round-trip serialization
   - Use custom matchers for cleaner assertions
   - Maintain golden files for compatibility testing
   - Mock at interface boundaries, not protobuf messages directly
   - Test both happy paths and error conditions
   - Measure and monitor serialization performance
   - Automate testing in CI/CD pipelines

### When to Use Each Testing Strategy

- **Unit Tests**: Every message operation, field access, and serialization
- **Integration Tests**: Cross-component message passing and service interactions
- **Property-Based Tests**: Discovering edge cases and ensuring invariants
- **Golden File Tests**: Verifying wire format stability across versions
- **Mocking**: Isolating components that depend on external protobuf services

### Common Pitfalls to Avoid

1. Not testing default value behavior
2. Assuming serialization is deterministic without verification
3. Over-mocking protobuf messages instead of interfaces
4. Ignoring wire format compatibility testing
5. Not testing with realistic data sizes
6. Forgetting to test repeated and nested fields thoroughly
7. Neglecting memory management in C implementations

By following these strategies and best practices, you can build robust, well-tested protobuf-based applications that maintain data integrity, handle edge cases gracefully, and evolve safely over time.