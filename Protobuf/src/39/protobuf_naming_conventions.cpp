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