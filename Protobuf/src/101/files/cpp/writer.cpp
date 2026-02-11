#include <iostream>
#include <fstream>
#include "messages.pb.h"

int main() {
    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    tutorial::AddressBook address_book;
    
    // Add a person
    tutorial::Person* person = address_book.add_people();
    person->set_name("John Doe");
    person->set_id(1234);
    person->set_email("john@example.com");
    
    tutorial::Person::PhoneNumber* phone = person->add_phones();
    phone->set_number("555-1234");
    phone->set_type(tutorial::Person::MOBILE);
    
    phone = person->add_phones();
    phone->set_number("555-5678");
    phone->set_type(tutorial::Person::WORK);
    
    // Write to file
    std::fstream output("addressbook.pb", std::ios::out | std::ios::binary);
    if (!address_book.SerializeToOstream(&output)) {
        std::cerr << "Failed to write address book." << std::endl;
        return -1;
    }
    
    std::cout << "Address book written successfully!" << std::endl;
    
    // Optional: Delete all global objects allocated by libprotobuf.
    google::protobuf::ShutdownProtobufLibrary();
    
    return 0;
}
