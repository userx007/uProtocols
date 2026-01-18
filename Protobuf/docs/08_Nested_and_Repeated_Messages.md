# Nested and Repeated Messages in Protocol Buffers

## Overview

Protocol Buffers supports two powerful features for organizing complex data structures: **nested messages** and **repeated fields**. Nested messages allow you to encapsulate related data within a message, creating hierarchical structures, while repeated fields enable you to store arrays or lists of values. Together, these features provide a flexible way to model real-world data relationships.

## Nested Messages

Nested messages are message types defined within another message. They provide encapsulation and help organize related data logically. You can think of them as inner classes or structs that belong to a specific parent message.

### Basic Syntax

```protobuf
syntax = "proto3";

message Person {
  string name = 1;
  int32 age = 2;
  
  message Address {
    string street = 1;
    string city = 2;
    string state = 3;
    string zip_code = 4;
  }
  
  Address home_address = 3;
  Address work_address = 4;
}
```

In this example, `Address` is nested within `Person`. The `Address` message is only accessible through `Person` and logically belongs to it.

## Repeated Fields

Repeated fields allow you to store zero or more values of the same type. They're equivalent to arrays or lists in programming languages. Any field type (primitive, message, enum) can be marked as repeated.

### Basic Syntax

```protobuf
message ShoppingCart {
  string user_id = 1;
  repeated string item_ids = 2;
  repeated double prices = 3;
}
```

## Combining Nested and Repeated Messages

The real power comes from combining these features to model complex data structures:

```protobuf
syntax = "proto3";

message Company {
  string name = 1;
  
  message Department {
    string dept_name = 1;
    
    message Employee {
      string name = 1;
      int32 employee_id = 2;
      string email = 3;
      repeated string skills = 4;
    }
    
    repeated Employee employees = 2;
  }
  
  repeated Department departments = 2;
}
```

## C/C++ Code Examples

### Example 1: Basic Nested Message

```cpp
#include <iostream>
#include <string>
#include "person.pb.h"

int main() {
    // Create a Person message with nested Address
    Person person;
    person.set_name("John Doe");
    person.set_age(30);
    
    // Access nested message using mutable_* method
    Person::Address* home = person.mutable_home_address();
    home->set_street("123 Main St");
    home->set_city("Springfield");
    home->set_state("IL");
    home->set_zip_code("62701");
    
    Person::Address* work = person.mutable_work_address();
    work->set_street("456 Corporate Blvd");
    work->set_city("Chicago");
    work->set_state("IL");
    work->set_zip_code("60601");
    
    // Read nested message data
    std::cout << person.name() << " lives at " 
              << person.home_address().street() << ", "
              << person.home_address().city() << std::endl;
    
    return 0;
}
```

### Example 2: Repeated Fields with Primitives

```cpp
#include <iostream>
#include "shopping.pb.h"

int main() {
    ShoppingCart cart;
    cart.set_user_id("user123");
    
    // Add items to repeated fields
    cart.add_item_ids("ITEM001");
    cart.add_item_ids("ITEM002");
    cart.add_item_ids("ITEM003");
    
    cart.add_prices(29.99);
    cart.add_prices(49.99);
    cart.add_prices(15.50);
    
    // Iterate through repeated fields
    std::cout << "Shopping cart for " << cart.user_id() << ":\n";
    for (int i = 0; i < cart.item_ids_size(); ++i) {
        std::cout << "  Item: " << cart.item_ids(i) 
                  << " - Price: $" << cart.prices(i) << std::endl;
    }
    
    // Access by index
    std::cout << "\nFirst item: " << cart.item_ids(0) << std::endl;
    
    // Modify existing element
    cart.set_prices(0, 24.99);
    
    return 0;
}
```

### Example 3: Repeated Nested Messages

```cpp
#include <iostream>
#include "company.pb.h"

int main() {
    Company company;
    company.set_name("Tech Corp");
    
    // Add first department
    Company::Department* engineering = company.add_departments();
    engineering->set_dept_name("Engineering");
    
    // Add employees to engineering
    Company::Department::Employee* emp1 = engineering->add_employees();
    emp1->set_name("Alice Smith");
    emp1->set_employee_id(1001);
    emp1->set_email("alice@techcorp.com");
    emp1->add_skills("C++");
    emp1->add_skills("Python");
    emp1->add_skills("Docker");
    
    Company::Department::Employee* emp2 = engineering->add_employees();
    emp2->set_name("Bob Johnson");
    emp2->set_employee_id(1002);
    emp2->set_email("bob@techcorp.com");
    emp2->add_skills("Rust");
    emp2->add_skills("Kubernetes");
    
    // Add second department
    Company::Department* sales = company.add_departments();
    sales->set_dept_name("Sales");
    
    Company::Department::Employee* emp3 = sales->add_employees();
    emp3->set_name("Carol White");
    emp3->set_employee_id(2001);
    emp3->set_email("carol@techcorp.com");
    emp3->add_skills("Negotiation");
    
    // Iterate through departments and employees
    std::cout << "Company: " << company.name() << "\n\n";
    for (const auto& dept : company.departments()) {
        std::cout << "Department: " << dept.dept_name() << std::endl;
        for (const auto& emp : dept.employees()) {
            std::cout << "  Employee: " << emp.name() 
                      << " (ID: " << emp.employee_id() << ")" << std::endl;
            std::cout << "  Skills: ";
            for (int i = 0; i < emp.skills_size(); ++i) {
                std::cout << emp.skills(i);
                if (i < emp.skills_size() - 1) std::cout << ", ";
            }
            std::cout << "\n" << std::endl;
        }
    }
    
    return 0;
}
```

## Rust Code Examples

### Example 1: Basic Nested Message

```rust
// Assuming generated code from person.proto
use person::{Person, person::Address};

fn main() {
    // Create a Person with nested Address
    let mut person = Person::default();
    person.name = "John Doe".to_string();
    person.age = 30;
    
    // Create and set home address
    let mut home = Address::default();
    home.street = "123 Main St".to_string();
    home.city = "Springfield".to_string();
    home.state = "IL".to_string();
    home.zip_code = "62701".to_string();
    person.home_address = Some(home);
    
    // Create and set work address
    let mut work = Address::default();
    work.street = "456 Corporate Blvd".to_string();
    work.city = "Chicago".to_string();
    work.state = "IL".to_string();
    work.zip_code = "60601".to_string();
    person.work_address = Some(work);
    
    // Read nested message data
    if let Some(ref home_addr) = person.home_address {
        println!("{} lives at {}, {}", 
                 person.name, home_addr.street, home_addr.city);
    }
}
```

### Example 2: Repeated Fields with Primitives

```rust
use shopping::ShoppingCart;

fn main() {
    let mut cart = ShoppingCart::default();
    cart.user_id = "user123".to_string();
    
    // Add items to repeated fields
    cart.item_ids.push("ITEM001".to_string());
    cart.item_ids.push("ITEM002".to_string());
    cart.item_ids.push("ITEM003".to_string());
    
    cart.prices.push(29.99);
    cart.prices.push(49.99);
    cart.prices.push(15.50);
    
    // Iterate through repeated fields
    println!("Shopping cart for {}:", cart.user_id);
    for (i, item_id) in cart.item_ids.iter().enumerate() {
        println!("  Item: {} - Price: ${}", item_id, cart.prices[i]);
    }
    
    // Access by index
    println!("\nFirst item: {}", cart.item_ids[0]);
    
    // Modify existing element
    cart.prices[0] = 24.99;
    
    // Use zip for cleaner iteration
    for (item, price) in cart.item_ids.iter().zip(cart.prices.iter()) {
        println!("{}: ${}", item, price);
    }
}
```

### Example 3: Repeated Nested Messages

```rust
use company::{Company, company::{Department, department::Employee}};

fn main() {
    let mut company = Company::default();
    company.name = "Tech Corp".to_string();
    
    // Create engineering department
    let mut engineering = Department::default();
    engineering.dept_name = "Engineering".to_string();
    
    // Add employees to engineering
    let mut emp1 = Employee::default();
    emp1.name = "Alice Smith".to_string();
    emp1.employee_id = 1001;
    emp1.email = "alice@techcorp.com".to_string();
    emp1.skills.push("C++".to_string());
    emp1.skills.push("Python".to_string());
    emp1.skills.push("Docker".to_string());
    engineering.employees.push(emp1);
    
    let mut emp2 = Employee::default();
    emp2.name = "Bob Johnson".to_string();
    emp2.employee_id = 1002;
    emp2.email = "bob@techcorp.com".to_string();
    emp2.skills.push("Rust".to_string());
    emp2.skills.push("Kubernetes".to_string());
    engineering.employees.push(emp2);
    
    company.departments.push(engineering);
    
    // Create sales department
    let mut sales = Department::default();
    sales.dept_name = "Sales".to_string();
    
    let mut emp3 = Employee::default();
    emp3.name = "Carol White".to_string();
    emp3.employee_id = 2001;
    emp3.email = "carol@techcorp.com".to_string();
    emp3.skills.push("Negotiation".to_string());
    sales.employees.push(emp3);
    
    company.departments.push(sales);
    
    // Iterate through departments and employees
    println!("Company: {}\n", company.name);
    for dept in &company.departments {
        println!("Department: {}", dept.dept_name);
        for emp in &dept.employees {
            println!("  Employee: {} (ID: {})", emp.name, emp.employee_id);
            print!("  Skills: ");
            for (i, skill) in emp.skills.iter().enumerate() {
                print!("{}", skill);
                if i < emp.skills.len() - 1 {
                    print!(", ");
                }
            }
            println!("\n");
        }
    }
}
```

### Example 4: Builder Pattern in Rust (More Idiomatic)

```rust
use company::{Company, company::{Department, department::Employee}};

fn create_employee(name: &str, id: i32, email: &str, skills: Vec<&str>) -> Employee {
    Employee {
        name: name.to_string(),
        employee_id: id,
        email: email.to_string(),
        skills: skills.iter().map(|s| s.to_string()).collect(),
    }
}

fn main() {
    let company = Company {
        name: "Tech Corp".to_string(),
        departments: vec![
            Department {
                dept_name: "Engineering".to_string(),
                employees: vec![
                    create_employee("Alice Smith", 1001, "alice@techcorp.com", 
                                  vec!["C++", "Python", "Docker"]),
                    create_employee("Bob Johnson", 1002, "bob@techcorp.com", 
                                  vec!["Rust", "Kubernetes"]),
                ],
            },
            Department {
                dept_name: "Sales".to_string(),
                employees: vec![
                    create_employee("Carol White", 2001, "carol@techcorp.com", 
                                  vec!["Negotiation"]),
                ],
            },
        ],
    };
    
    // Display company structure
    println!("Company: {}", company.name);
    for dept in &company.departments {
        println!("\n{} Department:", dept.dept_name);
        for emp in &dept.employees {
            println!("  {} - {}", emp.name, emp.skills.join(", "));
        }
    }
}
```

## Key Concepts and Best Practices

**Nested Messages:**
- Provide logical grouping and encapsulation
- Can be defined at any level of nesting
- In C++, access with `::` scope resolution (e.g., `Person::Address`)
- In Rust, access through module path (e.g., `person::Address`)
- Use `mutable_*()` in C++ to get a mutable pointer to nested messages

**Repeated Fields:**
- C++ provides `add_*()`, `*_size()`, index access, and range-based iteration
- Rust uses standard `Vec<T>` with all vector methods available
- Efficient for collections of any size
- Maintain insertion order
- Can contain primitives, messages, or enums

**Performance Considerations:**
- Repeated fields are backed by efficient containers (C++ uses `RepeatedField` or `RepeatedPtrField`, Rust uses `Vec`)
- Adding elements is generally O(1) amortized
- Nested messages add minimal overhead compared to flat structures
- Consider using `reserve()` in C++ or `Vec::with_capacity()` in Rust if you know the size ahead of time

## Summary

Nested and repeated messages are fundamental features of Protocol Buffers that enable modeling of complex, real-world data structures. Nested messages provide hierarchical organization and encapsulation, while repeated fields offer efficient arrays and collections. In C++, you work with generated methods like `add_*()`, `mutable_*()`, and iterators, while Rust provides a more natural interface using `Option<T>` for nested messages and `Vec<T>` for repeated fields. Together, these features make Protocol Buffers a powerful choice for serializing structured data across different programming languages while maintaining type safety and performance.