use std::fs;
use prost::Message;

pub mod tutorial {
    include!(concat!(env!("OUT_DIR"), "/tutorial.rs"));
}

use tutorial::{AddressBook, person::PhoneType};

fn main() -> std::io::Result<()> {
    let data = fs::read("addressbook.pb")?;
    
    let address_book = AddressBook::decode(&data[..])
        .expect("Failed to parse address book");
    
    for person in address_book.people {
        println!("Person ID: {}", person.id);
        println!("  Name: {}", person.name);
        println!("  E-mail: {}", person.email);
        
        for phone in person.phones {
            let phone_type = match PhoneType::try_from(phone.r#type) {
                Ok(PhoneType::Mobile) => "Mobile phone",
                Ok(PhoneType::Home) => "Home phone",
                Ok(PhoneType::Work) => "Work phone",
                _ => "Unknown",
            };
            println!("  {}: {}", phone_type, phone.number);
        }
    }
    
    Ok(())
}
