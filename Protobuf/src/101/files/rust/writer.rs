use std::fs::File;
use std::io::Write;
use prost::Message;

// Include the generated code
pub mod tutorial {
    include!(concat!(env!("OUT_DIR"), "/tutorial.rs"));
}

use tutorial::{AddressBook, Person, person::PhoneNumber, person::PhoneType};

fn main() -> std::io::Result<()> {
    let mut address_book = AddressBook::default();
    
    let phone1 = PhoneNumber {
        number: "555-1234".to_string(),
        r#type: PhoneType::Mobile as i32,
    };
    
    let phone2 = PhoneNumber {
        number: "555-5678".to_string(),
        r#type: PhoneType::Work as i32,
    };
    
    let person = Person {
        name: "John Doe".to_string(),
        id: 1234,
        email: "john@example.com".to_string(),
        phones: vec![phone1, phone2],
    };
    
    address_book.people.push(person);
    
    // Serialize to bytes
    let mut buf = Vec::new();
    address_book.encode(&mut buf).unwrap();
    
    // Write to file
    let mut file = File::create("addressbook.pb")?;
    file.write_all(&buf)?;
    
    println!("Address book written successfully!");
    
    Ok(())
}
