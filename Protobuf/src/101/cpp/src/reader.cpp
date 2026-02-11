#include <iostream>
#include <fstream>
#include "messages.pb.h"

void PrintAddressBook(const tutorial::AddressBook& address_book) {
    for (int i = 0; i < address_book.people_size(); i++) {
        const tutorial::Person& person = address_book.people(i);
        
        std::cout << "Person ID: " << person.id() << std::endl;
        std::cout << "  Name: " << person.name() << std::endl;
        std::cout << "  E-mail: " << person.email() << std::endl;
        
        for (int j = 0; j < person.phones_size(); j++) {
            const tutorial::Person::PhoneNumber& phone = person.phones(j);
            
            switch (phone.type()) {
                case tutorial::Person::MOBILE:
                    std::cout << "  Mobile phone: ";
                    break;
                case tutorial::Person::HOME:
                    std::cout << "  Home phone: ";
                    break;
                case tutorial::Person::WORK:
                    std::cout << "  Work phone: ";
                    break;
            }
            std::cout << phone.number() << std::endl;
        }
    }
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    tutorial::AddressBook address_book;
    
    std::fstream input("addressbook.pb", std::ios::in | std::ios::binary);
    if (!input) {
        std::cerr << "File not found." << std::endl;
        return -1;
    } else if (!address_book.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse address book." << std::endl;
        return -1;
    }
    
    PrintAddressBook(address_book);
    
    google::protobuf::ShutdownProtobufLibrary();
    
    return 0;
}
