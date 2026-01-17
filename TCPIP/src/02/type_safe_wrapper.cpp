#include <iostream>
#include <cstdint>
#include <arpa/inet.h>
#include <type_traits>

// Type-safe network byte order wrapper
template<typename T>
class NetworkOrder {
    static_assert(std::is_integral<T>::value, "T must be an integral type");
    
private:
    T value; // Stored in network byte order
    
public:
    // Constructor: converts from host to network order
    explicit NetworkOrder(T host_value) {
        if constexpr (sizeof(T) == 2) {
            value = htons(host_value);
        } else if constexpr (sizeof(T) == 4) {
            value = htonl(host_value);
        } else {
            value = host_value; // For single byte values
        }
    }
    
    // Convert back to host byte order
    T toHost() const {
        if constexpr (sizeof(T) == 2) {
            return ntohs(value);
        } else if constexpr (sizeof(T) == 4) {
            return ntohl(value);
        } else {
            return value;
        }
    }
    
    // Get raw network order value
    T raw() const {
        return value;
    }
    
    // Assignment operator
    NetworkOrder& operator=(T host_value) {
        if constexpr (sizeof(T) == 2) {
            value = htons(host_value);
        } else if constexpr (sizeof(T) == 4) {
            value = htonl(host_value);
        } else {
            value = host_value;
        }
        return *this;
    }
};

// Helper class for IP address handling
class IPAddress {
private:
    uint32_t addr; // Network byte order
    
public:
    IPAddress(const std::string& ip_str) {
        if (inet_pton(AF_INET, ip_str.c_str(), &addr) != 1) {
            throw std::runtime_error("Invalid IP address");
        }
    }
    
    IPAddress(uint32_t host_order_addr) {
        addr = htonl(host_order_addr);
    }
    
    uint32_t networkOrder() const { return addr; }
    uint32_t hostOrder() const { return ntohl(addr); }
    
    std::string toString() const {
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, str, INET_ADDRSTRLEN);
        return std::string(str);
    }
};

int main() {
    std::cout << "=== C++ Network Byte Order Demonstration ===" << std::endl;
    std::cout << std::endl;
    
    // Using the NetworkOrder wrapper
    NetworkOrder<uint16_t> port(8080);
    NetworkOrder<uint32_t> packet_size(1500);
    
    std::cout << "Port Number:" << std::endl;
    std::cout << "  Host order: " << port.toHost() << std::endl;
    std::cout << "  Network order (raw): 0x" << std::hex 
              << port.raw() << std::dec << std::endl;
    std::cout << std::endl;
    
    std::cout << "Packet Size:" << std::endl;
    std::cout << "  Host order: " << packet_size.toHost() << std::endl;
    std::cout << "  Network order (raw): 0x" << std::hex 
              << packet_size.raw() << std::dec << std::endl;
    std::cout << std::endl;
    
    // Using the IPAddress helper
    try {
        IPAddress ip1("192.168.1.1");
        IPAddress ip2(0xC0A80101); // Same as above in hex
        
        std::cout << "IP Address 1:" << std::endl;
        std::cout << "  String: " << ip1.toString() << std::endl;
        std::cout << "  Host order: 0x" << std::hex 
                  << ip1.hostOrder() << std::dec << std::endl;
        std::cout << "  Network order: 0x" << std::hex 
                  << ip1.networkOrder() << std::dec << std::endl;
        std::cout << std::endl;
        
        std::cout << "IP Address 2:" << std::endl;
        std::cout << "  String: " << ip2.toString() << std::endl;
        std::cout << "  Host order: 0x" << std::hex 
                  << ip2.hostOrder() << std::dec << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}