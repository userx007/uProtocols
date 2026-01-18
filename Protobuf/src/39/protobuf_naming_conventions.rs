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