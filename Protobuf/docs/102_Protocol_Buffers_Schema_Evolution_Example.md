# Protocol Buffers Schema Evolution Example

## Scenario: Service A and Service B communicating

### Initial State (Both use v1)

```protobuf
// person_v1.proto
message Person {
  string name = 1;
  int32 id = 2;
}
```

**Service A (sender)** creates:
```
name: "Alice"
id: 123
```

**Service B (receiver)** reads successfully ✅

---

## After Update: Service A upgrades to v2

```protobuf
// person_v2.proto
message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;        // NEW FIELD
  repeated string tags = 4; // NEW FIELD
}
```

**Service A (v2)** sends:
```
name: "Alice"
id: 123
email: "alice@example.com"
tags: ["developer", "rust"]
```

**Service B (still v1)** receives the message and:
- ✅ Reads `name` and `id` successfully
- ⚠️ Silently ignores `email` and `tags` (unknown fields)
- ✅ **No crash, no error!**

---

## Service B upgrades to v2

**Service A (v2)** sends:
```
name: "Bob"
id: 456
email: "bob@example.com"
tags: ["manager"]
```

**Service B (v2)** receives:
- ✅ Reads all fields successfully

---

## Old message sent by legacy client

**Old Client (v1)** sends:
```
name: "Charlie"
id: 789
```

**Service B (v2)** receives:
- ✅ Reads `name` and `id`
- ✅ Sets `email = ""` (default for string)
- ✅ Sets `tags = []` (empty list for repeated)
- ✅ **No crash, no error!**

---

## Key Insights

1. **Field numbers are the contract**, not field names
2. **Unknown fields are preserved** (proto3) or ignored gracefully
3. **Missing fields get default values**
4. **You can deploy services independently** without coordinating updates
5. **Gradual rollouts are safe**

---

## What BREAKS Compatibility

### ❌ Changing Field Number
```protobuf
// v1
message Person {
  string name = 1;
}

// v2 - BROKEN!
message Person {
  string name = 2;  // Changed number - OLD DATA UNREADABLE
}
```

### ❌ Changing Field Type Incompatibly
```protobuf
// v1
message Person {
  string name = 1;
}

// v2 - BROKEN!
message Person {
  int32 name = 1;  // Type changed - DATA CORRUPTION
}
```

### ❌ Reusing Field Numbers
```protobuf
// v1
message Person {
  string name = 1;
  string email = 2;
}

// v2 - removed email
message Person {
  string name = 1;
  // email removed
  int32 age = 2;  // DANGER! Reused number 2 for different purpose
}

// Correct way:
message Person {
  string name = 1;
  reserved 2;      // Reserve the number
  int32 age = 3;   // Use new number
}
```

---

## Comparison with JSON

| Feature | Protobuf | JSON |
|---------|----------|------|
| Schema required | Yes (proto file) | No (implicit) |
| Forward compatible | Yes (add fields) | Yes |
| Backward compatible | Yes (with care) | Yes |
| Field validation | At compile time | At runtime |
| Type safety | Strong | Weak |
| Size | Smaller (binary) | Larger (text) |
| Human readable | No | Yes |
| Schema evolution | Planned & safe | Risky without docs |

---

## Best Practices

1. **Never reuse field numbers** - use `reserved`
2. **Never change field numbers** - they're permanent IDs
3. **Document your schema** - explain the purpose of fields
4. **Version your services** - not the proto files themselves
5. **Test with old data** - ensure backward compatibility
6. **Use optional carefully** - in proto3, everything is optional by default
7. **Plan for growth** - leave gaps in numbering for future fields

---

## Summary

✅ Schema is required, but designed for evolution
✅ Services can update independently
✅ Old and new code can interoperate
❌ Field numbers must never change
❌ Some type changes are forbidden