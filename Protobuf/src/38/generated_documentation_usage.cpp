#include "user_service.pb.h"
#include <iostream>
#include <memory>

// When you compile the .proto file with protoc, the comments are preserved
// in the generated C++ code. Here's how to use the documented API:

int main() {
    // The User message class includes all documentation from the .proto file
    example::User user;
    
    // Setting fields - each setter has documentation from the .proto file
    // The 'id' field documentation mentions it's read-only and system-assigned
    user.set_id(12345);
    
    // The 'email' field documentation states it must be unique
    user.set_email("john.doe@example.com");
    
    // The 'name' field requires 1-50 characters according to documentation
    user.set_name("John Doe");
    
    // Setting the account status - enum values have their own documentation
    user.set_status(example::ACCOUNT_STATUS_ACTIVE);
    
    // The 'created_at' field is documented as Unix epoch seconds
    user.set_created_at(1704067200);  // Jan 1, 2024
    
    // Optional phone field - documentation specifies E.164 format
    user.set_phone("+14155552671");
    
    // Role defaults to ROLE_USER as documented
    user.set_role(example::ROLE_USER);
    
    // Display user info
    std::cout << "User ID: " << user.id() << std::endl;
    std::cout << "Email: " << user.email() << std::endl;
    std::cout << "Name: " << user.name() << std::endl;
    std::cout << "Status: " << user.status() << std::endl;
    std::cout << "Created: " << user.created_at() << std::endl;
    
    // Accessing documentation at runtime via descriptors
    const google::protobuf::Descriptor* descriptor = user.GetDescriptor();
    const google::protobuf::FieldDescriptor* email_field = 
        descriptor->FindFieldByName("email");
    
    if (email_field) {
        // Get the source code info (requires special compilation flag)
        std::cout << "\nField name: " << email_field->name() << std::endl;
        std::cout << "Field number: " << email_field->number() << std::endl;
        std::cout << "Field type: " << email_field->type_name() << std::endl;
    }
    
    // Creating a request message
    example::CreateUserRequest request;
    request.set_email("jane@example.com");
    request.set_name("Jane Smith");
    request.set_phone("+14155552672");
    request.set_role(example::ROLE_MODERATOR);
    
    std::cout << "\nCreate User Request:" << std::endl;
    std::cout << "Email: " << request.email() << std::endl;
    std::cout << "Name: " << request.name() << std::endl;
    std::cout << "Role: " << request.role() << std::endl;
    
    // Enum reflection with documentation
    const google::protobuf::EnumDescriptor* role_enum = 
        example::Role_descriptor();
    
    std::cout << "\nAvailable Roles:" << std::endl;
    for (int i = 0; i < role_enum->value_count(); ++i) {
        const google::protobuf::EnumValueDescriptor* value = 
            role_enum->value(i);
        std::cout << "  " << value->name() 
                  << " = " << value->number() << std::endl;
    }
    
    return 0;
}

// Additional example: Using proto descriptors to access comments
// (requires compiling with --include_source_info flag)
void print_field_documentation() {
    const google::protobuf::Descriptor* desc = 
        example::User::descriptor();
    
    std::cout << "\nUser Message Fields:" << std::endl;
    for (int i = 0; i < desc->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* field = desc->field(i);
        std::cout << "  Field: " << field->name() 
                  << " (tag " << field->number() << ")" << std::endl;
        // Note: Accessing actual comment text requires SourceCodeInfo
        // which is available through the FileDescriptor
    }
}