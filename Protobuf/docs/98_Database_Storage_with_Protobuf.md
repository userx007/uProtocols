# Database Storage with Protocol Buffers

**Main Topics:**
- Why store Protobuf in databases (advantages and use cases)
- Three storage strategies: Binary BLOB, Hybrid, and Field Decomposition
- Indexing approaches with SQL examples
- Query patterns for field extraction, membership checking, and range queries

**Code Examples:**

**C/C++:**
- Complete SQLite integration with full CRUD operations
- Batch operations with transactions
- Memory-efficient arena allocators
- Error handling and serialization best practices

**Rust:**
- Synchronous implementation with rusqlite
- Async implementation with SQLx and Tokio
- PostgreSQL integration example
- Type-safe protobuf handling with prost

**Best Practices:**
- Schema evolution strategies
- Indexing optimization
- Data versioning
- Compression techniques
- Transaction handling
- Performance optimization
- Migration patterns
- Monitoring and debugging

The document includes working code examples showing how to insert, retrieve, update, delete, and query protobuf-serialized data in databases, along with practical recommendations for production use.

## Overview

Protocol Buffers provide a serialization format suitable for long-term data storage, making them an excellent choice for database persistence. This document explores strategies for storing serialized protobuf messages in databases, indexing approaches, and query patterns.

## Table of Contents

1. [Why Store Protobuf in Databases](#why-store-protobuf-in-databases)
2. [Storage Strategies](#storage-strategies)
3. [Indexing Approaches](#indexing-approaches)
4. [Query Patterns](#query-patterns)
5. [C/C++ Implementation Examples](#cc-implementation-examples)
6. [Rust Implementation Examples](#rust-implementation-examples)
7. [Best Practices](#best-practices)
8. [Summary](#summary)

## Why Store Protobuf in Databases

### Advantages

1. **Compact Storage**: Protobuf serializes data into a binary format, which is much more compact than text-based formats like JSON or XML, reducing storage and bandwidth usage

2. **Schema Evolution**: Easy to add or remove fields without breaking existing data or requiring complex migrations

3. **Performance**: Protobuf's serialization and deserialization processes are typically faster when compared to JSON

4. **Type Safety**: Protobuf requires developers to define a clear schema for data in .proto files, leading to better consistency and easier maintenance

5. **Cross-Platform**: Data can be written in one language and read in another

### Use Cases

- Storing structured application data
- Event sourcing and audit logs
- Configuration management
- Microservices data persistence
- Time-series data storage

## Storage Strategies

### 1. Binary BLOB Storage

Store the entire serialized protobuf message as a binary BLOB in a database column.

**Pros:**
- Simple implementation
- Preserves all protobuf features
- Easy schema evolution

**Cons:**
- Limited queryability without extensions
- Requires deserialization for field access

### 2. Hybrid Approach

Store both the binary protobuf and extracted key fields in separate columns.

**Pros:**
- Fast queries on indexed fields
- Full protobuf benefits retained
- Flexible querying

**Cons:**
- Data duplication
- Additional storage overhead

### 3. Field Decomposition

Extract and store individual fields in traditional database columns.

**Pros:**
- Full SQL query capabilities
- Standard database indexing

**Cons:**
- Loses protobuf schema evolution benefits
- Complex mapping logic

## Indexing Strategies

### 1. Field Extraction with Generated Columns

Create indexed virtual columns that extract specific fields from the protobuf binary.

```sql
-- PostgreSQL example with stored generated column
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    user_data BYTEA NOT NULL,
    email TEXT GENERATED ALWAYS AS (
        protobuf_extract(user_data, 'User', '$.email')
    ) STORED
);

CREATE INDEX idx_user_email ON users(email);
```

### 2. Partial Indexing

PostgreSQL extensions support indexing Protobuf fields, allowing efficient querying based on specific parts of the Protobuf message

### 3. Composite Indexes

Combine multiple extracted fields for complex queries:

```sql
CREATE INDEX idx_user_composite ON users(
    protobuf_extract(user_data, 'User', '$.status'),
    protobuf_extract(user_data, 'User', '$.created_at')
);
```

## Query Patterns

### Pattern 1: Direct Field Extraction

Extract specific fields during queries:

```sql
SELECT 
    id,
    protobuf_extract(user_data, 'User', '$.name') AS name,
    protobuf_extract(user_data, 'User', '$.email') AS email
FROM users
WHERE protobuf_extract(user_data, 'User', '$.status') = 'active';
```

### Pattern 2: Membership Checking

Check for membership of arbitrary attributes and their values without unpacking the binary completely

### Pattern 3: Range Queries

Requires unpacking numerical values:

```sql
SELECT * FROM events
WHERE CAST(protobuf_extract(event_data, 'Event', '$.timestamp') AS BIGINT) 
    BETWEEN 1609459200 AND 1640995200;
```

## C/C++ Implementation Examples

### Basic Setup

First, define a protobuf schema:

```protobuf
// user.proto
syntax = "proto3";

package example;

message User {
    int32 id = 1;
    string name = 2;
    string email = 3;
    string phone = 4;
    int64 created_at = 5;
    bool is_active = 6;
}

message UserList {
    repeated User users = 1;
}
```

### Example 1: SQLite Storage with C++

```cpp
#include <sqlite3.h>
#include <iostream>
#include <string>
#include "user.pb.h"

class UserDatabase {
private:
    sqlite3* db;
    
public:
    UserDatabase(const std::string& db_path) {
        int rc = sqlite3_open(db_path.c_str(), &db);
        if (rc) {
            std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
            exit(1);
        }
        
        // Create table with binary protobuf column
        const char* sql = 
            "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_data BLOB NOT NULL,"
            "email TEXT,"  // Extracted field for indexing
            "created_at INTEGER"  // Extracted field for indexing
            ");";
        
        char* err_msg = nullptr;
        rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error: " << err_msg << std::endl;
            sqlite3_free(err_msg);
        }
        
        // Create indexes
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_email ON users(email);", 
                     nullptr, nullptr, nullptr);
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_created_at ON users(created_at);", 
                     nullptr, nullptr, nullptr);
    }
    
    ~UserDatabase() {
        sqlite3_close(db);
    }
    
    // Insert a user
    bool insertUser(const example::User& user) {
        // Serialize protobuf to string
        std::string serialized;
        if (!user.SerializeToString(&serialized)) {
            std::cerr << "Failed to serialize user" << std::endl;
            return false;
        }
        
        // Prepare statement
        const char* sql = 
            "INSERT INTO users (user_data, email, created_at) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Failed to prepare statement" << std::endl;
            return false;
        }
        
        // Bind parameters
        sqlite3_bind_blob(stmt, 1, serialized.data(), serialized.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, user.email().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, user.created_at());
        
        // Execute
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        return rc == SQLITE_DONE;
    }
    
    // Retrieve user by ID
    bool getUserById(int id, example::User& user) {
        const char* sql = "SELECT user_data FROM users WHERE id = ?;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_int(stmt, 1, id);
        
        bool success = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int blob_size = sqlite3_column_bytes(stmt, 0);
            
            std::string serialized(static_cast<const char*>(blob), blob_size);
            success = user.ParseFromString(serialized);
        }
        
        sqlite3_finalize(stmt);
        return success;
    }
    
    // Query users by email
    std::vector<example::User> getUsersByEmail(const std::string& email) {
        std::vector<example::User> users;
        
        const char* sql = "SELECT user_data FROM users WHERE email = ?;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return users;
        }
        
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int blob_size = sqlite3_column_bytes(stmt, 0);
            
            example::User user;
            std::string serialized(static_cast<const char*>(blob), blob_size);
            if (user.ParseFromString(serialized)) {
                users.push_back(user);
            }
        }
        
        sqlite3_finalize(stmt);
        return users;
    }
    
    // Update user
    bool updateUser(int id, const example::User& user) {
        std::string serialized;
        if (!user.SerializeToString(&serialized)) {
            return false;
        }
        
        const char* sql = 
            "UPDATE users SET user_data = ?, email = ?, created_at = ? WHERE id = ?;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_blob(stmt, 1, serialized.data(), serialized.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, user.email().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, user.created_at());
        sqlite3_bind_int(stmt, 4, id);
        
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        return rc == SQLITE_DONE;
    }
    
    // Delete user
    bool deleteUser(int id) {
        const char* sql = "DELETE FROM users WHERE id = ?;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_int(stmt, 1, id);
        
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        return rc == SQLITE_DONE;
    }
    
    // Query active users created after timestamp
    std::vector<example::User> getActiveUsersSince(int64_t timestamp) {
        std::vector<example::User> users;
        
        const char* sql = 
            "SELECT user_data FROM users WHERE created_at > ? ORDER BY created_at DESC;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return users;
        }
        
        sqlite3_bind_int64(stmt, 1, timestamp);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int blob_size = sqlite3_column_bytes(stmt, 0);
            
            example::User user;
            std::string serialized(static_cast<const char*>(blob), blob_size);
            if (user.ParseFromString(serialized) && user.is_active()) {
                users.push_back(user);
            }
        }
        
        sqlite3_finalize(stmt);
        return users;
    }
};

// Usage example
int main() {
    // Initialize protobuf library
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    UserDatabase db("users.db");
    
    // Create a user
    example::User user;
    user.set_id(1);
    user.set_name("John Doe");
    user.set_email("john@example.com");
    user.set_phone("+1234567890");
    user.set_created_at(std::time(nullptr));
    user.set_is_active(true);
    
    // Insert
    if (db.insertUser(user)) {
        std::cout << "User inserted successfully" << std::endl;
    }
    
    // Retrieve
    example::User retrieved;
    if (db.getUserById(1, retrieved)) {
        std::cout << "Retrieved user: " << retrieved.name() << std::endl;
    }
    
    // Query by email
    auto users = db.getUsersByEmail("john@example.com");
    std::cout << "Found " << users.size() << " users with that email" << std::endl;
    
    // Cleanup
    google::protobuf::ShutdownProtobufLibrary();
    
    return 0;
}
```

### Example 2: Batch Operations with Transactions

```cpp
#include <sqlite3.h>
#include <vector>
#include "user.pb.h"

class BatchUserDatabase {
private:
    sqlite3* db;
    
public:
    BatchUserDatabase(const std::string& db_path) {
        sqlite3_open(db_path.c_str(), &db);
        // Setup same as before
    }
    
    // Insert multiple users in a transaction
    bool insertUsersBatch(const std::vector<example::User>& users) {
        // Begin transaction
        char* err_msg = nullptr;
        if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "Failed to begin transaction: " << err_msg << std::endl;
            sqlite3_free(err_msg);
            return false;
        }
        
        const char* sql = 
            "INSERT INTO users (user_data, email, created_at) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
        
        bool success = true;
        for (const auto& user : users) {
            std::string serialized;
            if (!user.SerializeToString(&serialized)) {
                success = false;
                break;
            }
            
            sqlite3_reset(stmt);
            sqlite3_bind_blob(stmt, 1, serialized.data(), serialized.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, user.email().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, user.created_at());
            
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                success = false;
                break;
            }
        }
        
        sqlite3_finalize(stmt);
        
        if (success) {
            sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        } else {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
        
        return success;
    }
    
    // Get all users with pagination
    std::vector<example::User> getUsersPaginated(int limit, int offset) {
        std::vector<example::User> users;
        
        const char* sql = 
            "SELECT user_data FROM users ORDER BY id LIMIT ? OFFSET ?;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return users;
        }
        
        sqlite3_bind_int(stmt, 1, limit);
        sqlite3_bind_int(stmt, 2, offset);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int blob_size = sqlite3_column_bytes(stmt, 0);
            
            example::User user;
            std::string serialized(static_cast<const char*>(blob), blob_size);
            if (user.ParseFromString(serialized)) {
                users.push_back(user);
            }
        }
        
        sqlite3_finalize(stmt);
        return users;
    }
};
```

## Rust Implementation Examples

### Basic Setup

Add dependencies to `Cargo.toml`:

```toml
[dependencies]
prost = "0.12"
rusqlite = { version = "0.31", features = ["bundled"] }
tokio = { version = "1", features = ["full"] }
sqlx = { version = "0.7", features = ["runtime-tokio-native-tls", "sqlite"] }

[build-dependencies]
prost-build = "0.12"
```

Define the protobuf schema in `build.rs`:

```rust
// build.rs
fn main() {
    prost_build::compile_protos(&["src/user.proto"], &["src/"]).unwrap();
}
```

### Example 1: SQLite with Rusqlite

```rust
use rusqlite::{params, Connection, Result};
use prost::Message;

// Include the generated protobuf code
pub mod example {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

pub struct UserDatabase {
    conn: Connection,
}

impl UserDatabase {
    pub fn new(db_path: &str) -> Result<Self> {
        let conn = Connection::open(db_path)?;
        
        // Create table
        conn.execute(
            "CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_data BLOB NOT NULL,
                email TEXT,
                created_at INTEGER
            )",
            [],
        )?;
        
        // Create indexes
        conn.execute(
            "CREATE INDEX IF NOT EXISTS idx_email ON users(email)",
            [],
        )?;
        
        conn.execute(
            "CREATE INDEX IF NOT EXISTS idx_created_at ON users(created_at)",
            [],
        )?;
        
        Ok(UserDatabase { conn })
    }
    
    // Insert a user
    pub fn insert_user(&self, user: &example::User) -> Result<i64> {
        // Serialize protobuf to bytes
        let mut buf = Vec::new();
        user.encode(&mut buf).map_err(|e| {
            rusqlite::Error::ToSqlConversionFailure(Box::new(e))
        })?;
        
        self.conn.execute(
            "INSERT INTO users (user_data, email, created_at) VALUES (?1, ?2, ?3)",
            params![buf, user.email, user.created_at],
        )?;
        
        Ok(self.conn.last_insert_rowid())
    }
    
    // Get user by ID
    pub fn get_user_by_id(&self, id: i64) -> Result<Option<example::User>> {
        let mut stmt = self.conn.prepare(
            "SELECT user_data FROM users WHERE id = ?1"
        )?;
        
        let mut rows = stmt.query(params![id])?;
        
        if let Some(row) = rows.next()? {
            let data: Vec<u8> = row.get(0)?;
            let user = example::User::decode(&data[..]).map_err(|e| {
                rusqlite::Error::FromSqlConversionFailure(
                    0,
                    rusqlite::types::Type::Blob,
                    Box::new(e),
                )
            })?;
            Ok(Some(user))
        } else {
            Ok(None)
        }
    }
    
    // Get users by email
    pub fn get_users_by_email(&self, email: &str) -> Result<Vec<example::User>> {
        let mut stmt = self.conn.prepare(
            "SELECT user_data FROM users WHERE email = ?1"
        )?;
        
        let user_iter = stmt.query_map(params![email], |row| {
            let data: Vec<u8> = row.get(0)?;
            Ok(data)
        })?;
        
        let mut users = Vec::new();
        for user_data in user_iter {
            if let Ok(data) = user_data {
                if let Ok(user) = example::User::decode(&data[..]) {
                    users.push(user);
                }
            }
        }
        
        Ok(users)
    }
    
    // Update user
    pub fn update_user(&self, id: i64, user: &example::User) -> Result<()> {
        let mut buf = Vec::new();
        user.encode(&mut buf).map_err(|e| {
            rusqlite::Error::ToSqlConversionFailure(Box::new(e))
        })?;
        
        self.conn.execute(
            "UPDATE users SET user_data = ?1, email = ?2, created_at = ?3 WHERE id = ?4",
            params![buf, user.email, user.created_at, id],
        )?;
        
        Ok(())
    }
    
    // Delete user
    pub fn delete_user(&self, id: i64) -> Result<()> {
        self.conn.execute(
            "DELETE FROM users WHERE id = ?1",
            params![id],
        )?;
        Ok(())
    }
    
    // Get active users since timestamp
    pub fn get_active_users_since(&self, timestamp: i64) -> Result<Vec<example::User>> {
        let mut stmt = self.conn.prepare(
            "SELECT user_data FROM users WHERE created_at > ?1 ORDER BY created_at DESC"
        )?;
        
        let user_iter = stmt.query_map(params![timestamp], |row| {
            let data: Vec<u8> = row.get(0)?;
            Ok(data)
        })?;
        
        let mut users = Vec::new();
        for user_data in user_iter {
            if let Ok(data) = user_data {
                if let Ok(user) = example::User::decode(&data[..]) {
                    if user.is_active {
                        users.push(user);
                    }
                }
            }
        }
        
        Ok(users)
    }
}

// Usage example
fn main() -> Result<()> {
    let db = UserDatabase::new("users.db")?;
    
    // Create a user
    let user = example::User {
        id: 1,
        name: "John Doe".to_string(),
        email: "john@example.com".to_string(),
        phone: "+1234567890".to_string(),
        created_at: chrono::Utc::now().timestamp(),
        is_active: true,
    };
    
    // Insert
    let user_id = db.insert_user(&user)?;
    println!("Inserted user with ID: {}", user_id);
    
    // Retrieve
    if let Some(retrieved) = db.get_user_by_id(user_id)? {
        println!("Retrieved user: {}", retrieved.name);
    }
    
    // Query by email
    let users = db.get_users_by_email("john@example.com")?;
    println!("Found {} users with that email", users.len());
    
    Ok(())
}
```

### Example 2: Async SQLite with SQLx

```rust
use sqlx::{sqlite::SqlitePool, Row};
use prost::Message;

pub struct AsyncUserDatabase {
    pool: SqlitePool,
}

impl AsyncUserDatabase {
    pub async fn new(database_url: &str) -> Result<Self, sqlx::Error> {
        let pool = SqlitePool::connect(database_url).await?;
        
        // Create table
        sqlx::query(
            "CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_data BLOB NOT NULL,
                email TEXT,
                created_at INTEGER
            )"
        )
        .execute(&pool)
        .await?;
        
        // Create indexes
        sqlx::query("CREATE INDEX IF NOT EXISTS idx_email ON users(email)")
            .execute(&pool)
            .await?;
        
        sqlx::query("CREATE INDEX IF NOT EXISTS idx_created_at ON users(created_at)")
            .execute(&pool)
            .await?;
        
        Ok(AsyncUserDatabase { pool })
    }
    
    // Insert a user
    pub async fn insert_user(&self, user: &example::User) -> Result<i64, sqlx::Error> {
        let mut buf = Vec::new();
        user.encode(&mut buf).map_err(|e| {
            sqlx::Error::Decode(Box::new(e))
        })?;
        
        let result = sqlx::query(
            "INSERT INTO users (user_data, email, created_at) VALUES (?1, ?2, ?3)"
        )
        .bind(buf)
        .bind(&user.email)
        .bind(user.created_at)
        .execute(&self.pool)
        .await?;
        
        Ok(result.last_insert_rowid())
    }
    
    // Get user by ID
    pub async fn get_user_by_id(&self, id: i64) -> Result<Option<example::User>, sqlx::Error> {
        let row = sqlx::query("SELECT user_data FROM users WHERE id = ?1")
            .bind(id)
            .fetch_optional(&self.pool)
            .await?;
        
        if let Some(row) = row {
            let data: Vec<u8> = row.get(0);
            let user = example::User::decode(&data[..]).map_err(|e| {
                sqlx::Error::Decode(Box::new(e))
            })?;
            Ok(Some(user))
        } else {
            Ok(None)
        }
    }
    
    // Batch insert with transaction
    pub async fn insert_users_batch(&self, users: &[example::User]) -> Result<(), sqlx::Error> {
        let mut tx = self.pool.begin().await?;
        
        for user in users {
            let mut buf = Vec::new();
            user.encode(&mut buf).map_err(|e| {
                sqlx::Error::Decode(Box::new(e))
            })?;
            
            sqlx::query(
                "INSERT INTO users (user_data, email, created_at) VALUES (?1, ?2, ?3)"
            )
            .bind(&buf)
            .bind(&user.email)
            .bind(user.created_at)
            .execute(&mut *tx)
            .await?;
        }
        
        tx.commit().await?;
        Ok(())
    }
    
    // Stream users with pagination
    pub async fn get_users_paginated(
        &self,
        limit: i64,
        offset: i64,
    ) -> Result<Vec<example::User>, sqlx::Error> {
        let rows = sqlx::query(
            "SELECT user_data FROM users ORDER BY id LIMIT ?1 OFFSET ?2"
        )
        .bind(limit)
        .bind(offset)
        .fetch_all(&self.pool)
        .await?;
        
        let mut users = Vec::new();
        for row in rows {
            let data: Vec<u8> = row.get(0);
            if let Ok(user) = example::User::decode(&data[..]) {
                users.push(user);
            }
        }
        
        Ok(users)
    }
}

// Async usage example
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let db = AsyncUserDatabase::new("sqlite://users.db").await?;
    
    // Create users
    let users = vec![
        example::User {
            id: 1,
            name: "Alice".to_string(),
            email: "alice@example.com".to_string(),
            phone: "+1111111111".to_string(),
            created_at: chrono::Utc::now().timestamp(),
            is_active: true,
        },
        example::User {
            id: 2,
            name: "Bob".to_string(),
            email: "bob@example.com".to_string(),
            phone: "+2222222222".to_string(),
            created_at: chrono::Utc::now().timestamp(),
            is_active: true,
        },
    ];
    
    // Batch insert
    db.insert_users_batch(&users).await?;
    println!("Inserted {} users", users.len());
    
    // Retrieve with pagination
    let page = db.get_users_paginated(10, 0).await?;
    println!("Retrieved {} users in first page", page.len());
    
    Ok(())
}
```

### Example 3: PostgreSQL with Custom Types (Rust)

```rust
use tokio_postgres::{Client, NoTls, Error};
use prost::Message;

pub struct PostgresUserDatabase {
    client: Client,
}

impl PostgresUserDatabase {
    pub async fn new(connection_string: &str) -> Result<Self, Error> {
        let (client, connection) = tokio_postgres::connect(connection_string, NoTls).await?;
        
        // Spawn connection handler
        tokio::spawn(async move {
            if let Err(e) = connection.await {
                eprintln!("Connection error: {}", e);
            }
        });
        
        // Create table
        client.execute(
            "CREATE TABLE IF NOT EXISTS users (
                id SERIAL PRIMARY KEY,
                user_data BYTEA NOT NULL,
                email TEXT,
                created_at BIGINT
            )",
            &[],
        ).await?;
        
        // Create indexes
        client.execute(
            "CREATE INDEX IF NOT EXISTS idx_email ON users(email)",
            &[],
        ).await?;
        
        client.execute(
            "CREATE INDEX IF NOT EXISTS idx_created_at ON users(created_at)",
            &[],
        ).await?;
        
        Ok(PostgresUserDatabase { client })
    }
    
    pub async fn insert_user(&self, user: &example::User) -> Result<i32, Error> {
        let mut buf = Vec::new();
        user.encode(&mut buf).unwrap();
        
        let row = self.client.query_one(
            "INSERT INTO users (user_data, email, created_at) 
             VALUES ($1, $2, $3) RETURNING id",
            &[&buf, &user.email, &user.created_at],
        ).await?;
        
        Ok(row.get(0))
    }
    
    pub async fn get_user_by_id(&self, id: i32) -> Result<Option<example::User>, Error> {
        let rows = self.client.query(
            "SELECT user_data FROM users WHERE id = $1",
            &[&id],
        ).await?;
        
        if let Some(row) = rows.first() {
            let data: Vec<u8> = row.get(0);
            if let Ok(user) = example::User::decode(&data[..]) {
                return Ok(Some(user));
            }
        }
        
        Ok(None)
    }
    
    pub async fn search_users(&self, email_pattern: &str) -> Result<Vec<example::User>, Error> {
        let rows = self.client.query(
            "SELECT user_data FROM users WHERE email ILIKE $1",
            &[&format!("%{}%", email_pattern)],
        ).await?;
        
        let mut users = Vec::new();
        for row in rows {
            let data: Vec<u8> = row.get(0);
            if let Ok(user) = example::User::decode(&data[..]) {
                users.push(user);
            }
        }
        
        Ok(users)
    }
}
```

## Best Practices

### 1. Schema Evolution

```protobuf
// Version 1
message User {
    int32 id = 1;
    string name = 2;
    string email = 3;
}

// Version 2 - Adding optional fields is safe
message User {
    int32 id = 1;
    string name = 2;
    string email = 3;
    string phone = 4;        // New optional field
    bool is_active = 5;      // New optional field
}
```

**Guidelines:**
- Never change field numbers
- Use reserved fields for deprecated ones
- Add new fields as optional
- Use `oneof` for mutually exclusive fields

### 2. Indexing Strategy

```sql
-- Index frequently queried fields
CREATE INDEX idx_user_email ON users(
    protobuf_extract(user_data, 'User', '$.email')
);

-- Composite index for complex queries
CREATE INDEX idx_user_status_created ON users(
    protobuf_extract(user_data, 'User', '$.is_active'),
    protobuf_extract(user_data, 'User', '$.created_at')
);

-- Partial index for active users only
CREATE INDEX idx_active_users ON users(email)
WHERE protobuf_extract(user_data, 'User', '$.is_active') = true;
```

### 3. Data Versioning

Store schema version in the protobuf:

```protobuf
message VersionedUser {
    int32 schema_version = 1;
    User user = 2;
}
```

### 4. Compression

For large messages, consider compression:

```cpp
// C++ example with zlib compression
#include <zlib.h>

std::string compressProtobuf(const google::protobuf::Message& msg) {
    std::string serialized;
    msg.SerializeToString(&serialized);
    
    uLongf compressed_size = compressBound(serialized.size());
    std::string compressed(compressed_size, '\0');
    
    compress(reinterpret_cast<Bytef*>(&compressed[0]), &compressed_size,
             reinterpret_cast<const Bytef*>(serialized.data()), serialized.size());
    
    compressed.resize(compressed_size);
    return compressed;
}
```

### 5. Transaction Handling

Always use transactions for batch operations:

```rust
// Rust example
pub async fn bulk_update(&self, updates: Vec<(i64, example::User)>) -> Result<(), Error> {
    let mut tx = self.pool.begin().await?;
    
    for (id, user) in updates {
        let mut buf = Vec::new();
        user.encode(&mut buf)?;
        
        sqlx::query("UPDATE users SET user_data = ?1 WHERE id = ?2")
            .bind(buf)
            .bind(id)
            .execute(&mut *tx)
            .await?;
    }
    
    tx.commit().await?;
    Ok(())
}
```

### 6. Error Handling

Implement robust error handling for serialization:

```cpp
bool safeDeserialize(const std::string& data, google::protobuf::Message& msg) {
    try {
        if (!msg.ParseFromString(data)) {
            std::cerr << "Failed to parse protobuf" << std::endl;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during deserialization: " << e.what() << std::endl;
        return false;
    }
}
```

### 7. Performance Optimization

**Memory Management:**
```cpp
// Reuse arena allocator for multiple messages
google::protobuf::Arena arena;
auto* user1 = google::protobuf::Arena::CreateMessage<example::User>(&arena);
auto* user2 = google::protobuf::Arena::CreateMessage<example::User>(&arena);
// All memory freed when arena goes out of scope
```

**Lazy Loading:**
```rust
// Only deserialize when needed
pub struct LazyUser {
    id: i64,
    data: Vec<u8>,
    cached_user: Option<example::User>,
}

impl LazyUser {
    pub fn get_user(&mut self) -> Result<&example::User, prost::DecodeError> {
        if self.cached_user.is_none() {
            self.cached_user = Some(example::User::decode(&self.data[..])?);
        }
        Ok(self.cached_user.as_ref().unwrap())
    }
}
```

### 8. Migration Patterns

**Safe field addition:**
```sql
-- No ALTER TABLE needed!
-- Just deploy new application code with updated .proto
```

**Field removal:**
```protobuf
message User {
    int32 id = 1;
    string name = 2;
    reserved 3;  // Previously 'deprecated_field'
    reserved "deprecated_field";
    string email = 4;
}
```

### 9. Monitoring and Debugging

```cpp
// Add logging for serialization size
void logSerializationStats(const google::protobuf::Message& msg) {
    size_t size = msg.ByteSizeLong();
    std::cout << "Serialized size: " << size << " bytes" << std::endl;
    std::cout << "Message type: " << msg.GetTypeName() << std::endl;
}
```

### 10. Connection Pooling

```rust
// Use connection pooling for better performance
use sqlx::sqlite::SqlitePoolOptions;

let pool = SqlitePoolOptions::new()
    .max_connections(10)
    .connect("sqlite://users.db")
    .await?;
```

## Summary

Database storage with Protocol Buffers offers a powerful combination of efficient serialization, schema flexibility, and cross-platform compatibility. Key takeaways:

**Advantages:**
- Compact binary storage reduces database size
- Schema evolution without complex migrations
- Fast serialization/deserialization
- Type safety and backward compatibility
- Cross-language interoperability

**Storage Approaches:**
1. **Binary BLOB**: Simple, preserves all protobuf features
2. **Hybrid**: Combines binary storage with extracted indexed fields
3. **Field Decomposition**: Traditional relational approach

**Best Practices:**
- Index frequently queried fields
- Use transactions for batch operations
- Implement proper error handling
- Version your schemas
- Consider compression for large messages
- Use connection pooling
- Monitor serialization performance

**Language-Specific Notes:**

**C/C++:**
- Use arena allocators for memory efficiency
- Leverage prepared statements
- Handle exceptions during serialization
- Clean up with `ShutdownProtobufLibrary()`

**Rust:**
- Leverage type safety with prost
- Use async operations with sqlx/tokio
- Implement proper error propagation
- Consider using connection pools

Protocol Buffers combined with modern databases provide an excellent foundation for building scalable, maintainable data storage systems that can evolve with your application's needs.