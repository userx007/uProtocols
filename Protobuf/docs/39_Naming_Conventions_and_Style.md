# Naming Conventions and Style in Protocol Buffers

Protocol Buffers has established naming conventions that promote consistency and readability across projects. Following these conventions is crucial for maintaining clean, professional code that integrates well with generated code in different languages.

## Core Naming Conventions

### 1. **Messages: CamelCase (PascalCase)**
Message type names should use CamelCase with the first letter capitalized:

```protobuf
message UserProfile { }
message OrderDetails { }
message PaymentTransaction { }
```

### 2. **Fields: snake_case (underscore_case)**
Field names should use lowercase with underscores separating words:

```protobuf
message UserProfile {
  string first_name = 1;
  string last_name = 2;
  int32 user_id = 3;
  string email_address = 4;
}
```

### 3. **Enums: UPPER_SNAKE_CASE for values**
Enum type names use CamelCase, while enum values use UPPER_SNAKE_CASE. The first enum value should typically be a zero value (often with a suffix like `_UNSPECIFIED`):

```protobuf
enum OrderStatus {
  ORDER_STATUS_UNSPECIFIED = 0;
  ORDER_STATUS_PENDING = 1;
  ORDER_STATUS_CONFIRMED = 2;
  ORDER_STATUS_SHIPPED = 3;
  ORDER_STATUS_DELIVERED = 4;
  ORDER_STATUS_CANCELLED = 5;
}
```

### 4. **Services and RPCs: CamelCase**
Service names and RPC method names use CamelCase:

```protobuf
service UserService {
  rpc GetUser(GetUserRequest) returns (GetUserResponse);
  rpc CreateUser(CreateUserRequest) returns (CreateUserResponse);
  rpc UpdateUserProfile(UpdateUserProfileRequest) returns (UpdateUserProfileResponse);
}
```

## Complete Example

```protobuf
syntax = "proto3";

package example.ecommerce;

// Message names: CamelCase
message Product {
  // Fields: snake_case
  int32 product_id = 1;
  string product_name = 2;
  string description = 3;
  double unit_price = 4;
  ProductCategory category = 5;
  repeated string image_urls = 6;
  int32 stock_quantity = 7;
}

// Enum type: CamelCase
// Enum values: UPPER_SNAKE_CASE with type prefix
enum ProductCategory {
  PRODUCT_CATEGORY_UNSPECIFIED = 0;
  PRODUCT_CATEGORY_ELECTRONICS = 1;
  PRODUCT_CATEGORY_CLOTHING = 2;
  PRODUCT_CATEGORY_BOOKS = 3;
  PRODUCT_CATEGORY_HOME_GARDEN = 4;
}

message OrderRequest {
  int32 user_id = 1;
  repeated int32 product_ids = 2;
  string shipping_address = 3;
  PaymentMethod payment_method = 4;
}

enum PaymentMethod {
  PAYMENT_METHOD_UNSPECIFIED = 0;
  PAYMENT_METHOD_CREDIT_CARD = 1;
  PAYMENT_METHOD_DEBIT_CARD = 2;
  PAYMENT_METHOD_PAYPAL = 3;
  PAYMENT_METHOD_BANK_TRANSFER = 4;
}
```

## Code Examples

### C++ Example

```cpp
#include <iostream>
#include <string>
#include "example.pb.h" // Generated from the .proto file

using namespace example::ecommerce;

void demonstrate_naming_conventions() {
    // Create a Product message (CamelCase in proto -> CamelCase in C++)
    Product product;
    
    // Set fields (snake_case in proto -> set_snake_case() in C++)
    product.set_product_id(12345);
    product.set_product_name("Wireless Headphones");
    product.set_description("High-quality wireless headphones with noise cancellation");
    product.set_unit_price(149.99);
    product.set_stock_quantity(50);
    
    // Set enum value (UPPER_SNAKE_CASE in proto -> Type_VALUE in C++)
    product.set_category(ProductCategory::PRODUCT_CATEGORY_ELECTRONICS);
    
    // Add repeated fields
    product.add_image_urls("https://example.com/image1.jpg");
    product.add_image_urls("https://example.com/image2.jpg");
    
    // Access fields (snake_case in proto -> snake_case() getter in C++)
    std::cout << "Product ID: " << product.product_id() << std::endl;
    std::cout << "Product Name: " << product.product_name() << std::endl;
    std::cout << "Unit Price: $" << product.unit_price() << std::endl;
    std::cout << "Stock: " << product.stock_quantity() << " units" << std::endl;
    
    // Enum comparison
    if (product.category() == ProductCategory::PRODUCT_CATEGORY_ELECTRONICS) {
        std::cout << "Category: Electronics" << std::endl;
    }
    
    // Iterate over repeated fields
    std::cout << "Image URLs:" << std::endl;
    for (int i = 0; i < product.image_urls_size(); ++i) {
        std::cout << "  - " << product.image_urls(i) << std::endl;
    }
    
    // Create OrderRequest
    OrderRequest order;
    order.set_user_id(9876);
    order.add_product_ids(12345);
    order.add_product_ids(12346);
    order.set_shipping_address("123 Main St, Anytown, USA");
    order.set_payment_method(PaymentMethod::PAYMENT_METHOD_CREDIT_CARD);
    
    std::cout << "\nOrder Details:" << std::endl;
    std::cout << "User ID: " << order.user_id() << std::endl;
    std::cout << "Products: " << order.product_ids_size() << " items" << std::endl;
    std::cout << "Shipping: " << order.shipping_address() << std::endl;
    
    // Enum to string (using generated enum descriptor)
    std::cout << "Payment: " << PaymentMethod_Name(order.payment_method()) << std::endl;
    
    // Check if field is set (proto3 defaults)
    if (order.has_payment_method()) {
        std::cout << "Payment method is explicitly set" << std::endl;
    }
}

// Example with mutable accessors
void modify_product(Product* product) {
    // Mutable accessor for repeated fields
    product->mutable_image_urls()->Clear();
    product->add_image_urls("https://example.com/new_image.jpg");
    
    // Direct field modification
    *product->mutable_description() = "Updated description";
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    demonstrate_naming_conventions();
    
    // Cleanup
    google::protobuf::ShutdownProtobufLibrary();
    
    return 0;
}
```

### Rust Example

```rust
// Generated code would be in a module, typically from prost or protobuf crate
// This example assumes using prost for code generation

use prost::Message;

// The generated Rust code follows Rust naming conventions
// Proto snake_case fields -> Rust snake_case
// Proto CamelCase messages -> Rust PascalCase (CamelCase)
// Proto UPPER_SNAKE_CASE enums -> Rust PascalCase enum with PascalCase variants

// Example of generated structures (simplified)
#[derive(Clone, PartialEq, Message)]
pub struct Product {
    #[prost(int32, tag = "1")]
    pub product_id: i32,
    
    #[prost(string, tag = "2")]
    pub product_name: String,
    
    #[prost(string, tag = "3")]
    pub description: String,
    
    #[prost(double, tag = "4")]
    pub unit_price: f64,
    
    #[prost(enumeration = "ProductCategory", tag = "5")]
    pub category: i32,
    
    #[prost(string, repeated, tag = "6")]
    pub image_urls: Vec<String>,
    
    #[prost(int32, tag = "7")]
    pub stock_quantity: i32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(i32)]
pub enum ProductCategory {
    Unspecified = 0,
    Electronics = 1,
    Clothing = 2,
    Books = 3,
    HomeGarden = 4,
}

#[derive(Clone, PartialEq, Message)]
pub struct OrderRequest {
    #[prost(int32, tag = "1")]
    pub user_id: i32,
    
    #[prost(int32, repeated, tag = "2")]
    pub product_ids: Vec<i32>,
    
    #[prost(string, tag = "3")]
    pub shipping_address: String,
    
    #[prost(enumeration = "PaymentMethod", tag = "4")]
    pub payment_method: i32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(i32)]
pub enum PaymentMethod {
    Unspecified = 0,
    CreditCard = 1,
    DebitCard = 2,
    Paypal = 3,
    BankTransfer = 4,
}

fn demonstrate_naming_conventions() {
    // Create a Product (struct names are PascalCase)
    let mut product = Product {
        // Fields use snake_case (matching proto definition)
        product_id: 12345,
        product_name: "Wireless Headphones".to_string(),
        description: "High-quality wireless headphones with noise cancellation".to_string(),
        unit_price: 149.99,
        category: ProductCategory::Electronics as i32,
        image_urls: vec![
            "https://example.com/image1.jpg".to_string(),
            "https://example.com/image2.jpg".to_string(),
        ],
        stock_quantity: 50,
    };
    
    // Access fields using snake_case
    println!("Product ID: {}", product.product_id);
    println!("Product Name: {}", product.product_name);
    println!("Unit Price: ${:.2}", product.unit_price);
    println!("Stock: {} units", product.stock_quantity);
    
    // Enum comparison (Rust uses PascalCase for enum variants)
    if product.category == ProductCategory::Electronics as i32 {
        println!("Category: Electronics");
    }
    
    // Iterate over repeated fields
    println!("Image URLs:");
    for url in &product.image_urls {
        println!("  - {}", url);
    }
    
    // Modify fields
    product.description = "Updated description with new features".to_string();
    product.image_urls.push("https://example.com/image3.jpg".to_string());
    
    // Create OrderRequest
    let order = OrderRequest {
        user_id: 9876,
        product_ids: vec![12345, 12346],
        shipping_address: "123 Main St, Anytown, USA".to_string(),
        payment_method: PaymentMethod::CreditCard as i32,
    };
    
    println!("\nOrder Details:");
    println!("User ID: {}", order.user_id);
    println!("Products: {} items", order.product_ids.len());
    println!("Shipping: {}", order.shipping_address);
    
    // Match on enum values
    match PaymentMethod::try_from(order.payment_method) {
        Ok(PaymentMethod::CreditCard) => println!("Payment: Credit Card"),
        Ok(PaymentMethod::DebitCard) => println!("Payment: Debit Card"),
        Ok(PaymentMethod::Paypal) => println!("Payment: PayPal"),
        Ok(PaymentMethod::BankTransfer) => println!("Payment: Bank Transfer"),
        _ => println!("Payment: Unknown"),
    }
}

// Implement TryFrom for enum conversion
impl TryFrom<i32> for ProductCategory {
    type Error = ();
    
    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(ProductCategory::Unspecified),
            1 => Ok(ProductCategory::Electronics),
            2 => Ok(ProductCategory::Clothing),
            3 => Ok(ProductCategory::Books),
            4 => Ok(ProductCategory::HomeGarden),
            _ => Err(()),
        }
    }
}

impl TryFrom<i32> for PaymentMethod {
    type Error = ();
    
    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(PaymentMethod::Unspecified),
            1 => Ok(PaymentMethod::CreditCard),
            2 => Ok(PaymentMethod::DebitCard),
            3 => Ok(PaymentMethod::Paypal),
            4 => Ok(PaymentMethod::BankTransfer),
            _ => Err(()),
        }
    }
}

// Example of serialization/deserialization
fn serialize_example() -> Result<(), Box<dyn std::error::Error>> {
    let product = Product {
        product_id: 12345,
        product_name: "Wireless Headphones".to_string(),
        description: "Premium audio device".to_string(),
        unit_price: 149.99,
        category: ProductCategory::Electronics as i32,
        image_urls: vec!["https://example.com/image.jpg".to_string()],
        stock_quantity: 50,
    };
    
    // Serialize to bytes
    let mut buf = Vec::new();
    product.encode(&mut buf)?;
    println!("Serialized {} bytes", buf.len());
    
    // Deserialize from bytes
    let decoded = Product::decode(&buf[..])?;
    println!("Decoded product: {}", decoded.product_name);
    
    Ok(())
}

fn main() {
    demonstrate_naming_conventions();
    
    if let Err(e) = serialize_example() {
        eprintln!("Serialization error: {}", e);
    }
}
```

## Key Style Guidelines

### **File Organization**
- One `.proto` file per major message type or service
- Group related messages in the same file
- Use packages to prevent naming conflicts: `package company.project.module;`

### **Comments and Documentation**
```protobuf
// UserProfile represents a user's account information
// and preferences within the system.
message UserProfile {
  // Unique identifier for the user
  int32 user_id = 1;
  
  // User's email address (must be valid format)
  string email_address = 2;
  
  // ISO 8601 timestamp of account creation
  string created_at = 3;
}
```

### **Field Numbering**
- Reserve 1-15 for frequently used fields (1 byte encoding)
- Use 16+ for less common fields
- Never reuse field numbers
- Use `reserved` for deprecated fields:

```protobuf
message LegacyUser {
  reserved 2, 5 to 10;
  reserved "old_field_name";
  
  int32 user_id = 1;
  string username = 3;
}
```

### **Enum Best Practices**
- Always include a zero value with `_UNSPECIFIED` suffix
- Prefix enum values with the enum type name to avoid collisions
- Use consistent prefixing across your project

```protobuf
enum NotificationPriority {
  NOTIFICATION_PRIORITY_UNSPECIFIED = 0;
  NOTIFICATION_PRIORITY_LOW = 1;
  NOTIFICATION_PRIORITY_MEDIUM = 2;
  NOTIFICATION_PRIORITY_HIGH = 3;
  NOTIFICATION_PRIORITY_URGENT = 4;
}
```

## Language-Specific Mappings

| Proto Style | C++ | Rust | Java | Python |
|-------------|-----|------|------|--------|
| `UserProfile` (message) | `UserProfile` | `UserProfile` | `UserProfile` | `UserProfile` |
| `user_id` (field) | `user_id()` / `set_user_id()` | `user_id` | `getUserId()` / `setUserId()` | `user_id` |
| `ORDER_STATUS_PENDING` (enum) | `ORDER_STATUS_PENDING` | `OrderStatus::Pending` | `ORDER_STATUS_PENDING` | `ORDER_STATUS_PENDING` |

## Summary

Protocol Buffers naming conventions ensure consistency and readability across different programming languages:

- **Messages**: Use CamelCase (PascalCase) for clear type identification
- **Fields**: Use snake_case for compatibility with multiple language conventions
- **Enums**: Type names in CamelCase, values in UPPER_SNAKE_CASE with type prefixes
- **Services/RPCs**: Use CamelCase for service-oriented clarity

Following these conventions makes generated code feel natural in each target language while maintaining consistency in the `.proto` definitions. The style guide helps teams collaborate effectively and ensures that protobuf definitions integrate smoothly with existing codebases across C++, Rust, Java, Python, Go, and other supported languages.