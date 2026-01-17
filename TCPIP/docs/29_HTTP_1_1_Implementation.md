# HTTP/1.1 Implementation: Building from Scratch

## Overview

HTTP/1.1 (Hypertext Transfer Protocol version 1.1) is an application-layer protocol that runs over TCP/IP. It defines how clients (typically web browsers) and servers communicate to exchange resources like HTML pages, images, and data. Building an HTTP implementation from scratch provides deep insight into web communication, request/response cycles, and the structure of modern web applications.

## Core Concepts

### HTTP Message Structure

HTTP messages consist of:
1. **Start line**: Request line (for requests) or status line (for responses)
2. **Headers**: Key-value pairs providing metadata
3. **Empty line**: CRLF (`\r\n`) separator
4. **Body** (optional): The actual data being transmitted

### Request Format
```
GET /index.html HTTP/1.1\r\n
Host: www.example.com\r\n
User-Agent: CustomClient/1.0\r\n
Accept: text/html\r\n
\r\n
```

### Response Format
```
HTTP/1.1 200 OK\r\n
Content-Type: text/html\r\n
Content-Length: 1234\r\n
\r\n
<html>...</html>
```

## Implementation Details

### Key Features of HTTP/1.1
- **Persistent connections**: Multiple requests over single TCP connection (Connection: keep-alive)
- **Chunked transfer encoding**: Streaming data without knowing total size upfront
- **Host header**: Required header enabling virtual hosting
- **Cache control**: Headers for managing cached content
- **Range requests**: Partial content retrieval

## C Implementation

### HTTP Client in C

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 4096

typedef struct {
    int status_code;
    char *headers;
    char *body;
    size_t body_length;
} HttpResponse;

// Parse HTTP response
HttpResponse* parse_http_response(const char *response, size_t len) {
    HttpResponse *resp = malloc(sizeof(HttpResponse));
    if (!resp) return NULL;
    
    // Find status code
    const char *status_line = response;
    sscanf(status_line, "HTTP/%*s %d", &resp->status_code);
    
    // Find header/body separator
    const char *body_start = strstr(response, "\r\n\r\n");
    if (body_start) {
        body_start += 4; // Skip the \r\n\r\n
        size_t header_len = body_start - response;
        
        resp->headers = strndup(response, header_len);
        resp->body_length = len - header_len;
        resp->body = malloc(resp->body_length + 1);
        memcpy(resp->body, body_start, resp->body_length);
        resp->body[resp->body_length] = '\0';
    } else {
        resp->headers = strdup(response);
        resp->body = NULL;
        resp->body_length = 0;
    }
    
    return resp;
}

// HTTP GET request
HttpResponse* http_get(const char *host, int port, const char *path) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE * 10];
    ssize_t total_received = 0;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return NULL;
    }
    
    // Resolve hostname
    server = gethostbyname(host);
    if (!server) {
        fprintf(stderr, "Host resolution failed\n");
        close(sockfd);
        return NULL;
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);
    
    // Connect
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return NULL;
    }
    
    // Build HTTP request
    snprintf(request, BUFFER_SIZE,
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: CustomHTTPClient/1.0\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);
    
    // Send request
    if (send(sockfd, request, strlen(request), 0) < 0) {
        perror("Send failed");
        close(sockfd);
        return NULL;
    }
    
    // Receive response
    ssize_t bytes_received;
    while ((bytes_received = recv(sockfd, response + total_received, 
                                  sizeof(response) - total_received - 1, 0)) > 0) {
        total_received += bytes_received;
    }
    response[total_received] = '\0';
    
    close(sockfd);
    
    return parse_http_response(response, total_received);
}

void free_http_response(HttpResponse *resp) {
    if (resp) {
        free(resp->headers);
        free(resp->body);
        free(resp);
    }
}

int main() {
    HttpResponse *resp = http_get("example.com", 80, "/");
    
    if (resp) {
        printf("Status Code: %d\n", resp->status_code);
        printf("Headers:\n%s\n", resp->headers);
        printf("Body:\n%s\n", resp->body);
        free_http_response(resp);
    }
    
    return 0;
}
```

### HTTP Server in C

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_PENDING 10

typedef struct {
    char method[16];
    char path[256];
    char version[16];
    char headers[BUFFER_SIZE];
} HttpRequest;

// Parse HTTP request
HttpRequest* parse_http_request(const char *request) {
    HttpRequest *req = malloc(sizeof(HttpRequest));
    if (!req) return NULL;
    
    // Parse request line
    sscanf(request, "%s %s %s", req->method, req->path, req->version);
    
    // Store headers
    const char *header_start = strchr(request, '\n');
    if (header_start) {
        strncpy(req->headers, header_start + 1, BUFFER_SIZE - 1);
        req->headers[BUFFER_SIZE - 1] = '\0';
    }
    
    return req;
}

// Send HTTP response
void send_response(int client_sock, int status_code, 
                   const char *status_text, const char *content_type,
                   const char *body) {
    char response[BUFFER_SIZE];
    size_t body_len = body ? strlen(body) : 0;
    
    snprintf(response, BUFFER_SIZE,
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             status_code, status_text, content_type, body_len,
             body ? body : "");
    
    send(client_sock, response, strlen(response), 0);
}

// Handle client connection
void* handle_client(void *arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        
        HttpRequest *req = parse_http_request(buffer);
        
        if (req) {
            printf("Request: %s %s %s\n", req->method, req->path, req->version);
            
            if (strcmp(req->method, "GET") == 0) {
                if (strcmp(req->path, "/") == 0) {
                    const char *html = "<html><body><h1>Hello from C HTTP Server!</h1></body></html>";
                    send_response(client_sock, 200, "OK", "text/html", html);
                } else if (strcmp(req->path, "/api/status") == 0) {
                    const char *json = "{\"status\":\"running\",\"version\":\"1.0\"}";
                    send_response(client_sock, 200, "OK", "application/json", json);
                } else {
                    send_response(client_sock, 404, "Not Found", "text/plain", 
                                "404 - Page Not Found");
                }
            } else {
                send_response(client_sock, 405, "Method Not Allowed", "text/plain",
                            "Method Not Allowed");
            }
            
            free(req);
        }
    }
    
    close(client_sock);
    return NULL;
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    // Listen
    if (listen(server_sock, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    printf("HTTP Server listening on port %d\n", PORT);
    
    // Accept connections
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Handle each client in a new thread
        pthread_t thread;
        int *client_sock_ptr = malloc(sizeof(int));
        *client_sock_ptr = client_sock;
        
        if (pthread_create(&thread, NULL, handle_client, client_sock_ptr) != 0) {
            perror("Thread creation failed");
            close(client_sock);
            free(client_sock_ptr);
        }
        pthread_detach(thread);
    }
    
    close(server_sock);
    return 0;
}
```

## C++ Implementation

### HTTP Client in C++

```cpp
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

class HttpResponse {
public:
    int status_code;
    std::string status_message;
    std::map<std::string, std::string> headers;
    std::string body;
    
    void print() const {
        std::cout << "Status: " << status_code << " " << status_message << "\n";
        std::cout << "Headers:\n";
        for (const auto& [key, value] : headers) {
            std::cout << "  " << key << ": " << value << "\n";
        }
        std::cout << "\nBody:\n" << body << "\n";
    }
};

class HttpClient {
private:
    std::string host;
    int port;
    
    static std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        size_t end = str.find_last_not_of(" \t\r\n");
        return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
    }
    
    HttpResponse parseResponse(const std::string& response) {
        HttpResponse resp;
        std::istringstream stream(response);
        std::string line;
        
        // Parse status line
        if (std::getline(stream, line)) {
            std::istringstream status_stream(line);
            std::string http_version;
            status_stream >> http_version >> resp.status_code;
            std::getline(status_stream, resp.status_message);
            resp.status_message = trim(resp.status_message);
        }
        
        // Parse headers
        while (std::getline(stream, line) && line != "\r") {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));
                resp.headers[key] = value;
            }
        }
        
        // Parse body
        std::ostringstream body_stream;
        while (std::getline(stream, line)) {
            body_stream << line << "\n";
        }
        resp.body = body_stream.str();
        
        return resp;
    }
    
public:
    HttpClient(const std::string& host, int port = 80) 
        : host(host), port(port) {}
    
    HttpResponse get(const std::string& path) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }
        
        struct hostent* server = gethostbyname(host.c_str());
        if (!server) {
            close(sockfd);
            throw std::runtime_error("Host resolution failed");
        }
        
        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        std::memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        server_addr.sin_port = htons(port);
        
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sockfd);
            throw std::runtime_error("Connection failed");
        }
        
        // Build request
        std::ostringstream request;
        request << "GET " << path << " HTTP/1.1\r\n"
                << "Host: " << host << "\r\n"
                << "User-Agent: CustomCppClient/1.0\r\n"
                << "Accept: */*\r\n"
                << "Connection: close\r\n"
                << "\r\n";
        
        std::string req_str = request.str();
        send(sockfd, req_str.c_str(), req_str.length(), 0);
        
        // Receive response
        std::string response;
        char buffer[4096];
        ssize_t bytes;
        
        while ((bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes] = '\0';
            response += buffer;
        }
        
        close(sockfd);
        return parseResponse(response);
    }
    
    HttpResponse post(const std::string& path, const std::string& body,
                     const std::string& content_type = "application/x-www-form-urlencoded") {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }
        
        struct hostent* server = gethostbyname(host.c_str());
        if (!server) {
            close(sockfd);
            throw std::runtime_error("Host resolution failed");
        }
        
        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        std::memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        server_addr.sin_port = htons(port);
        
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sockfd);
            throw std::runtime_error("Connection failed");
        }
        
        // Build POST request
        std::ostringstream request;
        request << "POST " << path << " HTTP/1.1\r\n"
                << "Host: " << host << "\r\n"
                << "User-Agent: CustomCppClient/1.0\r\n"
                << "Content-Type: " << content_type << "\r\n"
                << "Content-Length: " << body.length() << "\r\n"
                << "Connection: close\r\n"
                << "\r\n"
                << body;
        
        std::string req_str = request.str();
        send(sockfd, req_str.c_str(), req_str.length(), 0);
        
        // Receive response
        std::string response;
        char buffer[4096];
        ssize_t bytes;
        
        while ((bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes] = '\0';
            response += buffer;
        }
        
        close(sockfd);
        return parseResponse(response);
    }
};

int main() {
    try {
        HttpClient client("example.com");
        HttpResponse response = client.get("/");
        response.print();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```

### HTTP Server in C++

```cpp
#include <iostream>
#include <string>
#include <map>
#include <functional>
#include <sstream>
#include <thread>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
    
    static HttpRequest parse(const std::string& request) {
        HttpRequest req;
        std::istringstream stream(request);
        std::string line;
        
        // Parse request line
        if (std::getline(stream, line)) {
            std::istringstream req_line(line);
            req_line >> req.method >> req.path >> req.version;
        }
        
        // Parse headers
        while (std::getline(stream, line) && line != "\r") {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 2);
                if (!value.empty() && value.back() == '\r') {
                    value.pop_back();
                }
                req.headers[key] = value;
            }
        }
        
        // Parse body
        std::ostringstream body_stream;
        while (std::getline(stream, line)) {
            body_stream << line;
        }
        req.body = body_stream.str();
        
        return req;
    }
};

class HttpResponse {
public:
    int status_code;
    std::string status_message;
    std::map<std::string, std::string> headers;
    std::string body;
    
    HttpResponse(int code = 200, const std::string& msg = "OK")
        : status_code(code), status_message(msg) {
        headers["Connection"] = "close";
    }
    
    void setBody(const std::string& content, const std::string& content_type = "text/plain") {
        body = content;
        headers["Content-Type"] = content_type;
        headers["Content-Length"] = std::to_string(body.length());
    }
    
    std::string toString() const {
        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_message << "\r\n";
        
        for (const auto& [key, value] : headers) {
            response << key << ": " << value << "\r\n";
        }
        
        response << "\r\n" << body;
        return response.str();
    }
};

class HttpServer {
private:
    int port;
    int server_socket;
    std::map<std::string, std::function<HttpResponse(const HttpRequest&)>> routes;
    
    void handleClient(int client_socket) {
        char buffer[4096];
        ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            HttpRequest request = HttpRequest::parse(buffer);
            
            std::cout << request.method << " " << request.path << "\n";
            
            HttpResponse response;
            
            std::string route_key = request.method + " " + request.path;
            auto it = routes.find(route_key);
            
            if (it != routes.end()) {
                response = it->second(request);
            } else {
                response = HttpResponse(404, "Not Found");
                response.setBody("404 - Page Not Found", "text/plain");
            }
            
            std::string resp_str = response.toString();
            send(client_socket, resp_str.c_str(), resp_str.length(), 0);
        }
        
        close(client_socket);
    }
    
public:
    HttpServer(int port) : port(port), server_socket(-1) {}
    
    void get(const std::string& path, std::function<HttpResponse(const HttpRequest&)> handler) {
        routes["GET " + path] = handler;
    }
    
    void post(const std::string& path, std::function<HttpResponse(const HttpRequest&)> handler) {
        routes["POST " + path] = handler;
    }
    
    void start() {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            throw std::runtime_error("Socket creation failed");
        }
        
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        
        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(server_socket);
            throw std::runtime_error("Bind failed");
        }
        
        if (listen(server_socket, 10) < 0) {
            close(server_socket);
            throw std::runtime_error("Listen failed");
        }
        
        std::cout << "HTTP Server listening on port " << port << "\n";
        
        while (true) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_socket < 0) {
                continue;
            }
            
            std::thread(&HttpServer::handleClient, this, client_socket).detach();
        }
    }
    
    ~HttpServer() {
        if (server_socket >= 0) {
            close(server_socket);
        }
    }
};

int main() {
    HttpServer server(8080);
    
    server.get("/", [](const HttpRequest& req) {
        HttpResponse resp;
        resp.setBody("<html><body><h1>Hello from C++ Server!</h1></body></html>", 
                     "text/html");
        return resp;
    });
    
    server.get("/api/info", [](const HttpRequest& req) {
        HttpResponse resp;
        resp.setBody("{\"name\":\"C++ HTTP Server\",\"version\":\"1.0\"}", 
                     "application/json");
        return resp;
    });
    
    server.post("/api/echo", [](const HttpRequest& req) {
        HttpResponse resp;
        resp.setBody(req.body, "application/json");
        return resp;
    });
    
    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

### HTTP Client in Rust

```rust
use std::io::{Read, Write};
use std::net::TcpStream;
use std::collections::HashMap;

#[derive(Debug)]
pub struct HttpResponse {
    pub status_code: u16,
    pub status_message: String,
    pub headers: HashMap<String, String>,
    pub body: String,
}

impl HttpResponse {
    fn parse(response: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let mut lines = response.lines();
        
        // Parse status line
        let status_line = lines.next().ok_or("Empty response")?;
        let parts: Vec<&str> = status_line.split_whitespace().collect();
        if parts.len() < 3 {
            return Err("Invalid status line".into());
        }
        
        let status_code = parts[1].parse::<u16>()?;
        let status_message = parts[2..].join(" ");
        
        // Parse headers
        let mut headers = HashMap::new();
        for line in lines.by_ref() {
            if line.is_empty() {
                break;
            }
            if let Some(colon_pos) = line.find(':') {
                let key = line[..colon_pos].trim().to_string();
                let value = line[colon_pos + 1..].trim().to_string();
                headers.insert(key, value);
            }
        }
        
        // Remaining is the body
        let body = lines.collect::<Vec<&str>>().join("\n");
        
        Ok(HttpResponse {
            status_code,
            status_message,
            headers,
            body,
        })
    }
}

pub struct HttpClient {
    host: String,
    port: u16,
}

impl HttpClient {
    pub fn new(host: &str, port: u16) -> Self {
        Self {
            host: host.to_string(),
            port,
        }
    }
    
    pub fn get(&self, path: &str) -> Result<HttpResponse, Box<dyn std::error::Error>> {
        let addr = format!("{}:{}", self.host, self.port);
        let mut stream = TcpStream::connect(addr)?;
        
        // Build request
        let request = format!(
            "GET {} HTTP/1.1\r\n\
             Host: {}\r\n\
             User-Agent: RustHttpClient/1.0\r\n\
             Accept: */*\r\n\
             Connection: close\r\n\
             \r\n",
            path, self.host
        );
        
        // Send request
        stream.write_all(request.as_bytes())?;
        
        // Read response
        let mut response = String::new();
        stream.read_to_string(&mut response)?;
        
        HttpResponse::parse(&response)
    }
    
    pub fn post(&self, path: &str, body: &str, content_type: &str) 
        -> Result<HttpResponse, Box<dyn std::error::Error>> {
        let addr = format!("{}:{}", self.host, self.port);
        let mut stream = TcpStream::connect(addr)?;
        
        // Build POST request
        let request = format!(
            "POST {} HTTP/1.1\r\n\
             Host: {}\r\n\
             User-Agent: RustHttpClient/1.0\r\n\
             Content-Type: {}\r\n\
             Content-Length: {}\r\n\
             Connection: close\r\n\
             \r\n\
             {}",
            path, self.host, content_type, body.len(), body
        );
        
        // Send request
        stream.write_all(request.as_bytes())?;
        
        // Read response
        let mut response = String::new();
        stream.read_to_string(&mut response)?;
        
        HttpResponse::parse(&response)
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = HttpClient::new("example.com", 80);
    let response = client.get("/")?;
    
    println!("Status: {} {}", response.status_code, response.status_message);
    println!("\nHeaders:");
    for (key, value) in &response.headers {
        println!("  {}: {}", key, value);
    }
    println!("\nBody:\n{}", response.body);
    
    Ok(())
}
```

### HTTP Server in Rust

```rust
use std::collections::HashMap;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::thread;

#[derive(Debug)]
pub struct HttpRequest {
    pub method: String,
    pub path: String,
    pub version: String,
    pub headers: HashMap<String, String>,
    pub body: String,
}

impl HttpRequest {
    fn parse(request: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let mut lines = request.lines();
        
        // Parse request line
        let request_line = lines.next().ok_or("Empty request")?;
        let parts: Vec<&str> = request_line.split_whitespace().collect();
        if parts.len() != 3 {
            return Err("Invalid request line".into());
        }
        
        let method = parts[0].to_string();
        let path = parts[1].to_string();
        let version = parts[2].to_string();
        
        // Parse headers
        let mut headers = HashMap::new();
        for line in lines.by_ref() {
            if line.is_empty() {
                break;
            }
            if let Some(colon_pos) = line.find(':') {
                let key = line[..colon_pos].trim().to_string();
                let value = line[colon_pos + 1..].trim().to_string();
                headers.insert(key, value);
            }
        }
        
        // Remaining is the body
        let body = lines.collect::<Vec<&str>>().join("\n");
        
        Ok(HttpRequest {
            method,
            path,
            version,
            headers,
            body,
        })
    }
}

pub struct HttpResponse {
    status_code: u16,
    status_message: String,
    headers: HashMap<String, String>,
    body: String,
}

impl HttpResponse {
    pub fn new(status_code: u16, status_message: &str) -> Self {
        let mut headers = HashMap::new();
        headers.insert("Connection".to_string(), "close".to_string());
        
        Self {
            status_code,
            status_message: status_message.to_string(),
            headers,
            body: String::new(),
        }
    }
    
    pub fn set_body(&mut self, body: String, content_type: &str) {
        self.headers.insert("Content-Type".to_string(), content_type.to_string());
        self.headers.insert("Content-Length".to_string(), body.len().to_string());
        self.body = body;
    }
    
    fn to_string(&self) -> String {
        let mut response = format!(
            "HTTP/1.1 {} {}\r\n",
            self.status_code, self.status_message
        );
        
        for (key, value) in &self.headers {
            response.push_str(&format!("{}: {}\r\n", key, value));
        }
        
        response.push_str("\r\n");
        response.push_str(&self.body);
        
        response
    }
}

type Handler = Arc<dyn Fn(&HttpRequest) -> HttpResponse + Send + Sync>;

pub struct HttpServer {
    port: u16,
    routes: HashMap<String, Handler>,
}

impl HttpServer {
    pub fn new(port: u16) -> Self {
        Self {
            port,
            routes: HashMap::new(),
        }
    }
    
    pub fn get<F>(&mut self, path: &str, handler: F)
    where
        F: Fn(&HttpRequest) -> HttpResponse + Send + Sync + 'static,
    {
        let key = format!("GET {}", path);
        self.routes.insert(key, Arc::new(handler));
    }
    
    pub fn post<F>(&mut self, path: &str, handler: F)
    where
        F: Fn(&HttpRequest) -> HttpResponse + Send + Sync + 'static,
    {
        let key = format!("POST {}", path);
        self.routes.insert(key, Arc::new(handler));
    }
    
    fn handle_client(mut stream: TcpStream, routes: Arc<HashMap<String, Handler>>) {
        let mut buffer = [0u8; 4096];
        
        if let Ok(bytes_read) = stream.read(&mut buffer) {
            if bytes_read == 0 {
                return;
            }
            
            let request_str = String::from_utf8_lossy(&buffer[..bytes_read]);
            
            if let Ok(request) = HttpRequest::parse(&request_str) {
                println!("{} {}", request.method, request.path);
                
                let route_key = format!("{} {}", request.method, request.path);
                
                let response = if let Some(handler) = routes.get(&route_key) {
                    handler(&request)
                } else {
                    let mut resp = HttpResponse::new(404, "Not Found");
                    resp.set_body("404 - Page Not Found".to_string(), "text/plain");
                    resp
                };
                
                let _ = stream.write_all(response.to_string().as_bytes());
            }
        }
    }
    
    pub fn start(self) -> Result<(), Box<dyn std::error::Error>> {
        let listener = TcpListener::bind(format!("0.0.0.0:{}", self.port))?;
        println!("HTTP Server listening on port {}", self.port);
        
        let routes = Arc::new(self.routes);
        
        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    let routes_clone = Arc::clone(&routes);
                    thread::spawn(move || {
                        Self::handle_client(stream, routes_clone);
                    });
                }
                Err(e) => {
                    eprintln!("Connection failed: {}", e);
                }
            }
        }
        
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut server = HttpServer::new(8080);
    
    server.get("/", |_req| {
        let mut resp = HttpResponse::new(200, "OK");
        resp.set_body(
            "<html><body><h1>Hello from Rust Server!</h1></body></html>".to_string(),
            "text/html"
        );
        resp
    });
    
    server.get("/api/info", |_req| {
        let mut resp = HttpResponse::new(200, "OK");
        resp.set_body(
            r#"{"name":"Rust HTTP Server","version":"1.0"}"#.to_string(),
            "application/json"
        );
        resp
    });
    
    server.post("/api/echo", |req| {
        let mut resp = HttpResponse::new(200, "OK");
        resp.set_body(req.body.clone(), "application/json");
        resp
    });
    
    server.start()
}
```

## Summary

Building an HTTP/1.1 implementation from scratch provides deep understanding of:

**Key Takeaways:**
- HTTP is a text-based request-response protocol built on TCP
- Proper parsing requires careful handling of CRLF line endings and header/body separation
- HTTP/1.1 supports persistent connections, chunked encoding, and virtual hosting
- Error handling is critical for robust client/server implementations
- Threading or async I/O is essential for handling multiple concurrent connections

**Practical Applications:**
- Custom HTTP clients for API testing and automation
- Lightweight embedded web servers for IoT devices
- Educational tools for understanding web protocols
- Debugging proxies and middleware
- Protocol analysis and network monitoring tools

**Production Considerations:**
- These examples are educational; production systems should use established libraries (libcurl, nginx, tokio-hyper)
- Real implementations need comprehensive error handling, security features (TLS/SSL), timeout management, and compliance with RFC 7230-7235
- Consider connection pooling, keep-alive management, and proper resource cleanup
- Handle edge cases like large payloads, slow clients, and malformed requests

The implementations above demonstrate the fundamental mechanics of HTTP, but production systems require additional robustness, security hardening, and performance optimization.