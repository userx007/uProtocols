// Bidirectional TCP Server Example with Protocol Buffers
// This demonstrates how the SAME sender can also be a receiver

#include <iostream>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "messages.pb.h"

class ProtobufServer {
private:
    int server_socket;
    int client_socket;
    
public:
    ProtobufServer(int port) {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        bind(server_socket, (sockaddr*)&address, sizeof(address));
        listen(server_socket, 1);
        
        std::cout << "Server listening on port " << port << std::endl;
    }
    
    void acceptConnection() {
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
        std::cout << "Client connected!" << std::endl;
    }
    
    // Server can SEND messages
    void sendPerson(const tutorial::Person& person) {
        std::string serialized;
        person.SerializeToString(&serialized);
        
        // Send size first (4 bytes)
        uint32_t size = htonl(serialized.size());
        send(client_socket, &size, sizeof(size), 0);
        
        // Send data
        send(client_socket, serialized.data(), serialized.size(), 0);
        
        std::cout << "[SERVER SENT] Person: " << person.name() << std::endl;
    }
    
    // Server can RECEIVE messages
    bool receivePerson(tutorial::Person& person) {
        // Receive size first
        uint32_t size;
        int bytes = recv(client_socket, &size, sizeof(size), 0);
        if (bytes <= 0) return false;
        
        size = ntohl(size);
        
        // Receive data
        char* buffer = new char[size];
        bytes = recv(client_socket, buffer, size, 0);
        
        bool success = person.ParseFromArray(buffer, bytes);
        delete[] buffer;
        
        if (success) {
            std::cout << "[SERVER RECEIVED] Person: " << person.name() << std::endl;
        }
        
        return success;
    }
    
    ~ProtobufServer() {
        close(client_socket);
        close(server_socket);
    }
};

class ProtobufClient {
private:
    int socket_fd;
    
public:
    ProtobufClient(const std::string& host, int port) {
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
        
        connect(socket_fd, (sockaddr*)&server_addr, sizeof(server_addr));
        std::cout << "Connected to server!" << std::endl;
    }
    
    // Client can SEND messages
    void sendPerson(const tutorial::Person& person) {
        std::string serialized;
        person.SerializeToString(&serialized);
        
        uint32_t size = htonl(serialized.size());
        send(socket_fd, &size, sizeof(size), 0);
        send(socket_fd, serialized.data(), serialized.size(), 0);
        
        std::cout << "[CLIENT SENT] Person: " << person.name() << std::endl;
    }
    
    // Client can RECEIVE messages
    bool receivePerson(tutorial::Person& person) {
        uint32_t size;
        int bytes = recv(socket_fd, &size, sizeof(size), 0);
        if (bytes <= 0) return false;
        
        size = ntohl(size);
        
        char* buffer = new char[size];
        bytes = recv(socket_fd, buffer, size, 0);
        
        bool success = person.ParseFromArray(buffer, bytes);
        delete[] buffer;
        
        if (success) {
            std::cout << "[CLIENT RECEIVED] Person: " << person.name() << std::endl;
        }
        
        return success;
    }
    
    ~ProtobufClient() {
        close(socket_fd);
    }
};

// Demonstration
int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " [server|client]" << std::endl;
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "server") {
        ProtobufServer server(8080);
        server.acceptConnection();
        
        // SERVER acts as BOTH sender AND receiver
        
        // Receive message from client
        tutorial::Person received_person;
        server.receivePerson(received_person);
        
        // Send response back to client
        tutorial::Person response;
        response.set_name("Server Response");
        response.set_id(9999);
        response.set_email("server@example.com");
        server.sendPerson(response);
        
        std::cout << "\n✓ Server demonstrated bidirectional communication!" << std::endl;
        
    } else if (mode == "client") {
        sleep(1); // Give server time to start
        ProtobufClient client("127.0.0.1", 8080);
        
        // CLIENT acts as BOTH sender AND receiver
        
        // Send message to server
        tutorial::Person request;
        request.set_name("Alice");
        request.set_id(123);
        request.set_email("alice@example.com");
        client.sendPerson(request);
        
        // Receive response from server
        tutorial::Person response;
        client.receivePerson(response);
        
        std::cout << "\n✓ Client demonstrated bidirectional communication!" << std::endl;
    }
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
