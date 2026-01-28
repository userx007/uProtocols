# Canonical Encoding Rules in Protocol Buffers

## Overview

Canonical Encoding Rules (CER) refers to a deterministic serialization approach that ensures the same message always produces identical byte sequences. This is critical for applications requiring content verification, cryptographic signatures, and content-addressed storage where the exact binary representation matters.

Unlike standard protobuf encoding which allows multiple valid representations of the same logical message (due to field ordering flexibility, unknown field handling, and implementation variations), canonical encoding enforces strict rules to guarantee byte-for-byte reproducibility.

## Why Canonical Encoding Matters

**Cryptographic Signatures**: When signing protobuf messages, you need the exact same bytes every time. Without canonical encoding, the same logical message could serialize differently, breaking signature verification.

**Content-Addressed Storage**: Systems like Git, IPFS, or blockchain use content hashes as identifiers. Non-deterministic encoding would generate different hashes for identical data.

**Caching and Deduplication**: Identifying duplicate content requires consistent serialization.

## Standard Protobuf Limitations

Standard protobuf serialization isn't deterministic because:
- Field order in serialized output can vary
- Unknown fields may be preserved differently
- Map iteration order is unspecified
- Extensions can appear in different orders
- Some implementations add padding or use different varint encodings

## Achieving Canonical Encoding

### Core Principles

1. **Sorted Field Order**: Always serialize fields by field number in ascending order
2. **Deterministic Map Iteration**: Sort map entries by key
3. **Strip Unknown Fields**: Remove any unrecognized fields before serialization
4. **Consistent Defaults**: Handle default values uniformly
5. **Normalized Representation**: Use the shortest valid encoding for numbers

## C++ Implementation

```cpp
#include <google/protobuf/message.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/util/message_differencer.h>
#include <string>
#include <vector>

// Example message definition (in .proto file):
// message Document {
//   string title = 1;
//   int64 timestamp = 2;
//   map<string, string> metadata = 3;
//   bytes content = 4;
// }

class CanonicalSerializer {
public:
    // Serialize message deterministically
    static bool SerializeCanonical(const google::protobuf::Message& message, 
                                   std::string* output) {
        // Create a copy to avoid modifying the original
        std::unique_ptr<google::protobuf::Message> canonical_copy(message.New());
        canonical_copy->CopyFrom(message);
        
        // Remove unknown fields to ensure determinism
        canonical_copy->DiscardUnknownFields();
        
        // Use deterministic serialization options
        google::protobuf::io::StringOutputStream string_stream(output);
        google::protobuf::io::CodedOutputStream coded_stream(&string_stream);
        
        // Enable deterministic mode - this ensures:
        // - Fields are written in tag order
        // - Maps are serialized in sorted order
        coded_stream.SetSerializationDeterministic(true);
        
        if (!canonical_copy->SerializeToCodedStream(&coded_stream)) {
            return false;
        }
        
        return true;
    }
    
    // Compute cryptographic hash of canonical representation
    static std::string ComputeCanonicalHash(const google::protobuf::Message& message) {
        std::string canonical_bytes;
        if (!SerializeCanonical(message, &canonical_bytes)) {
            return "";
        }
        
        // Use your preferred hash function (SHA-256, etc.)
        return ComputeSHA256(canonical_bytes);
    }
    
    // Verify signature against canonical representation
    static bool VerifySignature(const google::protobuf::Message& message,
                               const std::string& signature,
                               const std::string& public_key) {
        std::string canonical_bytes;
        if (!SerializeCanonical(message, &canonical_bytes)) {
            return false;
        }
        
        return CryptoVerify(canonical_bytes, signature, public_key);
    }

private:
    static std::string ComputeSHA256(const std::string& data) {
        // Placeholder - implement with OpenSSL or similar
        // Example: use EVP_Digest with EVP_sha256()
        return "hash_placeholder";
    }
    
    static bool CryptoVerify(const std::string& data, 
                            const std::string& signature,
                            const std::string& public_key) {
        // Placeholder - implement with crypto library
        return true;
    }
};

// Usage example
int main() {
    Document doc;
    doc.set_title("Canonical Example");
    doc.set_timestamp(1234567890);
    (*doc.mutable_metadata())["author"] = "Alice";
    (*doc.mutable_metadata())["version"] = "1.0";
    doc.set_content("Important content");
    
    // Serialize canonically
    std::string canonical_bytes;
    CanonicalSerializer::SerializeCanonical(doc, &canonical_bytes);
    
    // This will always produce the same bytes
    std::string canonical_bytes2;
    CanonicalSerializer::SerializeCanonical(doc, &canonical_bytes2);
    
    assert(canonical_bytes == canonical_bytes2);
    
    // Compute content hash for content-addressed storage
    std::string content_id = CanonicalSerializer::ComputeCanonicalHash(doc);
    
    return 0;
}
```

### Advanced C++ Features

```cpp
// Template for any protobuf message type
template<typename MessageType>
class CanonicalMessageHandler {
public:
    static std::string Serialize(const MessageType& msg) {
        std::string output;
        google::protobuf::io::StringOutputStream stream(&output);
        google::protobuf::io::CodedOutputStream coded(&stream);
        coded.SetSerializationDeterministic(true);
        
        MessageType copy = msg;
        copy.DiscardUnknownFields();
        copy.SerializeToCodedStream(&coded);
        
        return output;
    }
    
    // Compare two messages by their canonical form
    static bool CanonicalEquals(const MessageType& a, const MessageType& b) {
        return Serialize(a) == Serialize(b);
    }
};
```

## Rust Implementation

```rust
use prost::Message;
use sha2::{Sha256, Digest};
use std::collections::BTreeMap;

// Example: Using prost-generated structs
// In your .proto file:
// message Document {
//   string title = 1;
//   int64 timestamp = 2;
//   map<string, string> metadata = 3;
//   bytes content = 4;
// }

#[derive(Clone, PartialEq, Message)]
pub struct Document {
    #[prost(string, tag = "1")]
    pub title: String,
    
    #[prost(int64, tag = "2")]
    pub timestamp: i64,
    
    #[prost(map = "string, string", tag = "3")]
    pub metadata: std::collections::HashMap<String, String>,
    
    #[prost(bytes, tag = "4")]
    pub content: Vec<u8>,
}

/// Canonical serialization utilities
pub struct CanonicalEncoder;

impl CanonicalEncoder {
    /// Serialize message in canonical form
    pub fn serialize_canonical<M: Message>(message: &M) -> Vec<u8> {
        // prost serialization is deterministic by default when you
        // use BTreeMap instead of HashMap for map fields
        let mut buf = Vec::new();
        message.encode(&mut buf).expect("Encoding failed");
        buf
    }
    
    /// Compute SHA-256 hash of canonical representation
    pub fn compute_hash<M: Message>(message: &M) -> Vec<u8> {
        let canonical_bytes = Self::serialize_canonical(message);
        let mut hasher = Sha256::new();
        hasher.update(&canonical_bytes);
        hasher.finalize().to_vec()
    }
    
    /// Compute hex string hash for content addressing
    pub fn compute_content_id<M: Message>(message: &M) -> String {
        let hash = Self::compute_hash(message);
        hex::encode(hash)
    }
}

// Better approach: Use BTreeMap for deterministic ordering
#[derive(Clone, PartialEq, Message)]
pub struct CanonicalDocument {
    #[prost(string, tag = "1")]
    pub title: String,
    
    #[prost(int64, tag = "2")]
    pub timestamp: i64,
    
    #[prost(btree_map = "string, string", tag = "3")]
    pub metadata: BTreeMap<String, String>,
    
    #[prost(bytes, tag = "4")]
    pub content: Vec<u8>,
}

/// Content-addressed storage example
pub struct ContentAddressedStore {
    store: std::collections::HashMap<String, Vec<u8>>,
}

impl ContentAddressedStore {
    pub fn new() -> Self {
        Self {
            store: std::collections::HashMap::new(),
        }
    }
    
    /// Store document and return its content ID
    pub fn put<M: Message>(&mut self, message: &M) -> String {
        let content_id = CanonicalEncoder::compute_content_id(message);
        let canonical_bytes = CanonicalEncoder::serialize_canonical(message);
        self.store.insert(content_id.clone(), canonical_bytes);
        content_id
    }
    
    /// Retrieve document by content ID and decode
    pub fn get<M: Message + Default>(&self, content_id: &str) -> Option<M> {
        self.store.get(content_id).and_then(|bytes| {
            M::decode(&bytes[..]).ok()
        })
    }
}

/// Digital signature example
pub struct SignedMessage<M: Message> {
    pub message: M,
    pub signature: Vec<u8>,
}

impl<M: Message + Clone> SignedMessage<M> {
    pub fn sign(message: M, private_key: &[u8]) -> Self {
        let canonical_bytes = CanonicalEncoder::serialize_canonical(&message);
        let signature = sign_bytes(&canonical_bytes, private_key);
        
        Self { message, signature }
    }
    
    pub fn verify(&self, public_key: &[u8]) -> bool {
        let canonical_bytes = CanonicalEncoder::serialize_canonical(&self.message);
        verify_signature(&canonical_bytes, &self.signature, public_key)
    }
}

// Mock crypto functions (replace with real crypto library like ring or ed25519-dalek)
fn sign_bytes(data: &[u8], _private_key: &[u8]) -> Vec<u8> {
    // Placeholder - use actual signing library
    vec![0u8; 64]
}

fn verify_signature(_data: &[u8], _signature: &[u8], _public_key: &[u8]) -> bool {
    // Placeholder - use actual verification library
    true
}

// Usage example
fn main() {
    let mut doc = CanonicalDocument {
        title: "Canonical Example".to_string(),
        timestamp: 1234567890,
        metadata: BTreeMap::from([
            ("author".to_string(), "Alice".to_string()),
            ("version".to_string(), "1.0".to_string()),
        ]),
        content: b"Important content".to_vec(),
    };
    
    // Serialize canonically
    let bytes1 = CanonicalEncoder::serialize_canonical(&doc);
    let bytes2 = CanonicalEncoder::serialize_canonical(&doc);
    assert_eq!(bytes1, bytes2, "Canonical encoding is deterministic");
    
    // Content-addressed storage
    let mut store = ContentAddressedStore::new();
    let content_id = store.put(&doc);
    println!("Stored with content ID: {}", content_id);
    
    let retrieved: Option<CanonicalDocument> = store.get(&content_id);
    assert_eq!(retrieved, Some(doc.clone()));
    
    // Digital signatures
    let private_key = vec![0u8; 32]; // Mock key
    let public_key = vec![0u8; 32];  // Mock key
    
    let signed = SignedMessage::sign(doc, &private_key);
    assert!(signed.verify(&public_key));
}
```

## Best Practices

1. **Use BTreeMap for Maps**: In Rust, always use `BTreeMap` instead of `HashMap` for map fields to ensure sorted iteration.

2. **Discard Unknown Fields**: Always call `DiscardUnknownFields()` in C++ before canonical serialization.

3. **Enable Deterministic Mode**: In C++, use `SetSerializationDeterministic(true)` on `CodedOutputStream`.

4. **Validate Before Signing**: Ensure message is valid and complete before computing signatures.

5. **Version Your Schema**: Include version fields to handle schema evolution gracefully.

6. **Test Thoroughly**: Write tests that verify the same message produces identical bytes across different runs and implementations.

## Common Pitfalls

- **Timestamp Fields**: System-generated timestamps break determinism. Use explicit values.
- **Random IDs**: Auto-generated UUIDs make messages non-reproducible.
- **Floating Point**: Binary representation may vary; use fixed-point or string representations.
- **Map Ordering**: Standard HashMaps don't guarantee order; use sorted maps.

## Summary

Canonical Encoding Rules provide deterministic serialization for Protocol Buffers, essential for:

- **Cryptographic applications** requiring consistent byte representation for signing and verification
- **Content-addressed storage** where content hashes serve as identifiers
- **Distributed systems** needing reliable message comparison and deduplication

Key implementation requirements include sorted field serialization, deterministic map ordering, removal of unknown fields, and consistent encoding choices. Both C++ (via `SetSerializationDeterministic`) and Rust (via `prost` with `BTreeMap`) provide robust support for canonical encoding, enabling secure and reliable protobuf-based systems.