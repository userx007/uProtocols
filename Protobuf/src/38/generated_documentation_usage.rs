// When using prost or protobuf-codegen for Rust, the comments from .proto
// files are converted to Rust doc comments (///) that appear in generated code

// Generated code would look similar to this:

/// Represents a user account in the system.
/// Users are uniquely identified by their ID after creation.
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct User {
    /// Unique identifier assigned by the system.
    /// This field is read-only and set during user creation.
    #[prost(int64, tag = "1")]
    pub id: i64,
    
    /// User's email address. Must be unique across all users.
    /// Format: Standard RFC 5322 email format.
    #[prost(string, tag = "2")]
    pub email: String,
    
    /// User's display name (1-50 characters).
    #[prost(string, tag = "3")]
    pub name: String,
    
    /// Account status indicating if the user can access the system.
    #[prost(enumeration = "AccountStatus", tag = "4")]
    pub status: i32,
    
    /// Timestamp when the account was created (Unix epoch seconds).
    #[prost(int64, tag = "5")]
    pub created_at: i64,
    
    /// Optional phone number in E.164 format (e.g., +14155552671).
    #[prost(string, tag = "6")]
    pub phone: String,
    
    /// User's role determining their access permissions.
    #[prost(enumeration = "Role", tag = "7")]
    pub role: i32,
}

/// Defines the possible states of a user account.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord, ::prost::Enumeration)]
#[repr(i32)]
pub enum AccountStatus {
    /// Should not be used
    Unspecified = 0,
    /// Account is active and accessible
    Active = 1,
    /// Temporarily disabled by admin
    Suspended = 2,
    /// Soft-deleted, can be recovered
    Deleted = 3,
}

/// User roles determining access levels and permissions.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord, ::prost::Enumeration)]
#[repr(i32)]
pub enum Role {
    /// Should not be used
    Unspecified = 0,
    /// Standard user with basic permissions
    User = 1,
    /// Can moderate content and users
    Moderator = 2,
    /// Full system access
    Admin = 3,
}

/// Request message for creating a new user.
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct CreateUserRequest {
    /// Required. Email address for the new account.
    #[prost(string, tag = "1")]
    pub email: String,
    
    /// Required. Display name (1-50 characters).
    #[prost(string, tag = "2")]
    pub name: String,
    
    /// Optional. Phone number in E.164 format.
    #[prost(string, tag = "3")]
    pub phone: String,
    
    /// Optional. Role to assign. Defaults to ROLE_USER if not specified.
    #[prost(enumeration = "Role", tag = "4")]
    pub role: i32,
}

// Example usage demonstrating how documentation helps developers
fn main() {
    // Create a new user - the documentation tells us id is system-assigned
    let user = User {
        id: 12345,  // Documented as read-only, system-assigned
        email: "john.doe@example.com".to_string(),  // Must be unique
        name: "John Doe".to_string(),  // 1-50 characters
        status: AccountStatus::Active as i32,  // Active account
        created_at: 1704067200,  // Unix epoch as documented
        phone: "+14155552671".to_string(),  // E.164 format as specified
        role: Role::User as i32,  // Defaults to User role
    };
    
    println!("User ID: {}", user.id);
    println!("Email: {}", user.email);
    println!("Name: {}", user.name);
    println!("Status: {:?}", AccountStatus::try_from(user.status));
    println!("Created: {}", user.created_at);
    println!("Phone: {}", user.phone);
    
    // Create a user request
    let request = CreateUserRequest {
        email: "jane@example.com".to_string(),
        name: "Jane Smith".to_string(),
        phone: "+14155552672".to_string(),
        role: Role::Moderator as i32,  // Assigning moderator role
    };
    
    println!("\nCreate User Request:");
    println!("Email: {}", request.email);
    println!("Name: {}", request.name);
    println!("Role: {:?}", Role::try_from(request.role));
    
    // Working with enums - documentation provides context
    println!("\nAccount Status Values:");
    println!("Active: {}", AccountStatus::Active as i32);
    println!("Suspended: {}", AccountStatus::Suspended as i32);
    println!("Deleted: {}", AccountStatus::Deleted as i32);
    
    // Enum conversion with error handling
    match AccountStatus::try_from(user.status) {
        Ok(status) => println!("\nUser status: {:?}", status),
        Err(_) => println!("\nInvalid status value"),
    }
    
    // Pattern matching on roles
    match Role::try_from(user.role) {
        Ok(Role::Admin) => println!("User has full system access"),
        Ok(Role::Moderator) => println!("User can moderate content"),
        Ok(Role::User) => println!("User has basic permissions"),
        _ => println!("Unknown or invalid role"),
    }
}

// Example of using documentation in a service implementation
mod user_service {
    use super::*;
    
    /// Implementation of UserService operations
    pub struct UserServiceImpl;
    
    impl UserServiceImpl {
        /// Creates a new user account.
        /// 
        /// Returns the created user with a generated ID, or an error if:
        /// - The email is already registered
        /// - Required fields are missing
        /// - Email format is invalid
        pub fn create_user(&self, request: CreateUserRequest) -> Result<User, String> {
            // Validate email format (documented requirement)
            if !request.email.contains('@') {
                return Err("Invalid email format".to_string());
            }
            
            // Validate name length (1-50 characters as documented)
            if request.name.is_empty() || request.name.len() > 50 {
                return Err("Name must be 1-50 characters".to_string());
            }
            
            // Validate phone format if provided (E.164 format)
            if !request.phone.is_empty() && !request.phone.starts_with('+') {
                return Err("Phone must be in E.164 format".to_string());
            }
            
            // Create user with system-assigned ID
            let user = User {
                id: generate_id(),  // System-assigned as documented
                email: request.email,
                name: request.name,
                status: AccountStatus::Active as i32,
                created_at: current_timestamp(),
                phone: request.phone,
                role: request.role,
            };
            
            Ok(user)
        }
        
        /// Retrieves a user by their unique ID.
        /// Returns error if the user doesn't exist.
        pub fn get_user(&self, id: i64) -> Result<User, String> {
            // Implementation would query database
            Err(format!("User {} not found", id))
        }
    }
    
    fn generate_id() -> i64 {
        use std::time::{SystemTime, UNIX_EPOCH};
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as i64
    }
    
    fn current_timestamp() -> i64 {
        use std::time::{SystemTime, UNIX_EPOCH};
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as i64
    }
}