# REST: Representational State Transfer

## What is REST?

REST (Representational State Transfer) is an architectural style for designing networked applications. It was introduced by Roy Fielding in his 2000 doctoral dissertation. REST defines a set of constraints that, when applied to web services, create scalable, stateless, and cacheable systems.

REST is not a protocol or standard but rather a set of architectural principles. When applied to web services, these services are called RESTful web services. The primary medium for REST is HTTP, leveraging its existing methods and status codes.

## Core Principles of REST

### 1. Client-Server Architecture

The client and server are separate entities. The client handles the user interface and user experience, while the server manages data storage and business logic. This separation allows each to evolve independently.

### 2. Statelessness

Each request from the client to the server must contain all the information needed to understand and process the request. The server does not store any client context between requests. Session state is kept entirely on the client side.

**Benefits:**
- Improved scalability (servers don't need to maintain session state)
- Simplified server design
- Better reliability (failed requests can be easily retried)

**Trade-offs:**
- Increased overhead in requests
- Client must handle state management

### 3. Cacheability

Responses must define themselves as cacheable or non-cacheable. When a response is cacheable, the client can reuse that response data for equivalent requests in the future, reducing the number of interactions with the server.

### 4. Uniform Interface

This is the fundamental constraint that distinguishes REST from other architectural styles. It includes four sub-constraints:

**Resource Identification:** Resources are identified by URIs (Uniform Resource Identifiers). A resource can be anything: a document, image, temporal service, collection of other resources, or a non-virtual object.

**Resource Manipulation through Representations:** When a client holds a representation of a resource (including metadata), it has enough information to modify or delete the resource.

**Self-Descriptive Messages:** Each message includes enough information to describe how to process the message (e.g., media type specification).

**Hypermedia as the Engine of Application State (HATEOAS):** Clients interact with the application entirely through hypermedia provided dynamically by application servers.

### 5. Layered System

A client cannot ordinarily tell whether it is connected directly to the end server or an intermediary. Intermediary servers (proxies, gateways) can improve scalability by enabling load balancing and providing shared caches.

### 6. Code on Demand (Optional)

Servers can extend client functionality by transferring executable code (e.g., JavaScript). This is the only optional constraint.

## HTTP Methods in REST

RESTful APIs use standard HTTP methods to perform operations on resources:

**GET:** Retrieve a resource or collection of resources. Should be idempotent and safe (no side effects).

**POST:** Create a new resource. Not idempotent.

**PUT:** Update an existing resource (replace entirely). Should be idempotent.

**PATCH:** Partially update a resource. May or may not be idempotent depending on implementation.

**DELETE:** Remove a resource. Should be idempotent.

**HEAD:** Retrieve metadata about a resource (same as GET but without response body).

**OPTIONS:** Retrieve information about the communication options available for a resource.

## HTTP Status Codes

REST APIs use standard HTTP status codes to indicate the outcome of requests:

**2xx Success:**
- 200 OK: Request succeeded
- 201 Created: Resource created successfully
- 204 No Content: Success but no content to return

**3xx Redirection:**
- 301 Moved Permanently
- 304 Not Modified (cached version is still valid)

**4xx Client Errors:**
- 400 Bad Request: Invalid request syntax
- 401 Unauthorized: Authentication required
- 403 Forbidden: Server understood request but refuses to authorize
- 404 Not Found: Resource doesn't exist
- 409 Conflict: Request conflicts with current state

**5xx Server Errors:**
- 500 Internal Server Error
- 503 Service Unavailable

## Resource Naming Conventions

Good RESTful API design follows these naming conventions:

- Use nouns, not verbs (resources are things, actions are HTTP methods)
- Use plural nouns for collections: `/users`, `/products`
- Use hierarchical structure: `/users/123/orders/456`
- Use lowercase and hyphens: `/user-profiles` not `/userProfiles`
- Avoid deep nesting (generally no more than 2-3 levels)

**Good Examples:**
- `GET /users` - Get all users
- `GET /users/42` - Get user with ID 42
- `POST /users` - Create a new user
- `PUT /users/42` - Update user 42
- `DELETE /users/42` - Delete user 42
- `GET /users/42/orders` - Get orders for user 42

**Bad Examples:**
- `GET /getAllUsers`
- `POST /createUser`
- `GET /user/delete/42`

---

# REST API Implementation Examples

Now let's implement a simple REST API client in C, C++, and Rust. These examples will demonstrate making HTTP requests to a RESTful API.

## Example 1: C Implementation

This C example uses libcurl to make HTTP requests to a REST API.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* Structure to hold response data */
struct response_data {
    char *data;
    size_t size;
};

/* Callback function to handle response data */
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct response_data *mem = (struct response_data *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if(ptr == NULL) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

/* GET request */
int http_get(const char *url) {
    CURL *curl;
    CURLcode res;
    struct response_data chunk = {0};
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if(curl) {
        chunk.data = malloc(1);
        chunk.size = 0;
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
        } else {
            printf("GET Response:\n%s\n", chunk.data);
        }
        
        curl_easy_cleanup(curl);
        free(chunk.data);
    }
    
    curl_global_cleanup();
    return 0;
}

/* POST request */
int http_post(const char *url, const char *json_data) {
    CURL *curl;
    CURLcode res;
    struct response_data chunk = {0};
    struct curl_slist *headers = NULL;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if(curl) {
        chunk.data = malloc(1);
        chunk.size = 0;
        
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            printf("POST Response (HTTP %ld):\n%s\n", http_code, chunk.data);
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(chunk.data);
    }
    
    curl_global_cleanup();
    return 0;
}

/* PUT request */
int http_put(const char *url, const char *json_data) {
    CURL *curl;
    CURLcode res;
    struct response_data chunk = {0};
    struct curl_slist *headers = NULL;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if(curl) {
        chunk.data = malloc(1);
        chunk.size = 0;
        
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
        } else {
            printf("PUT Response:\n%s\n", chunk.data);
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(chunk.data);
    }
    
    curl_global_cleanup();
    return 0;
}

/* DELETE request */
int http_delete(const char *url) {
    CURL *curl;
    CURLcode res;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            printf("DELETE successful (HTTP %ld)\n", http_code);
        }
        
        curl_easy_cleanup(curl);
    }
    
    curl_global_cleanup();
    return 0;
}

int main(void) {
    const char *base_url = "https://jsonplaceholder.typicode.com";
    
    printf("=== REST API Examples in C ===\n\n");
    
    /* GET request - Fetch a user */
    printf("1. GET /users/1\n");
    char get_url[256];
    snprintf(get_url, sizeof(get_url), "%s/users/1", base_url);
    http_get(get_url);
    printf("\n");
    
    /* POST request - Create a new post */
    printf("2. POST /posts\n");
    const char *post_data = "{"
        "\"title\": \"My New Post\","
        "\"body\": \"This is the content of my post\","
        "\"userId\": 1"
    "}";
    char post_url[256];
    snprintf(post_url, sizeof(post_url), "%s/posts", base_url);
    http_post(post_url, post_data);
    printf("\n");
    
    /* PUT request - Update a post */
    printf("3. PUT /posts/1\n");
    const char *put_data = "{"
        "\"id\": 1,"
        "\"title\": \"Updated Title\","
        "\"body\": \"Updated content\","
        "\"userId\": 1"
    "}";
    char put_url[256];
    snprintf(put_url, sizeof(put_url), "%s/posts/1", base_url);
    http_put(put_url, put_data);
    printf("\n");
    
    /* DELETE request - Delete a post */
    printf("4. DELETE /posts/1\n");
    char delete_url[256];
    snprintf(delete_url, sizeof(delete_url), "%s/posts/1", base_url);
    http_delete(delete_url);
    
    return 0;
}
```

**Compilation:**
```bash
gcc -o rest_client_c rest_client.c -lcurl
```

## Example 2: C++ Implementation

This C++ example uses libcurl with a modern C++ wrapper approach.

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <curl/curl.h>

class RestClient {
private:
    std::string base_url;
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
    struct CurlDeleter {
        void operator()(CURL* curl) const {
            if (curl) curl_easy_cleanup(curl);
        }
    };
    
    using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;
    
public:
    RestClient(const std::string& url) : base_url(url) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    
    ~RestClient() {
        curl_global_cleanup();
    }
    
    struct Response {
        long status_code;
        std::string body;
        bool success;
    };
    
    Response get(const std::string& endpoint) {
        Response response;
        CurlPtr curl(curl_easy_init());
        
        if (!curl) {
            response.success = false;
            return response;
        }
        
        std::string url = base_url + endpoint;
        std::string response_body;
        
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "RestClient-CPP/1.0");
        
        CURLcode res = curl_easy_perform(curl.get());
        
        if (res != CURLE_OK) {
            std::cerr << "GET request failed: " << curl_easy_strerror(res) << std::endl;
            response.success = false;
            return response;
        }
        
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = response_body;
        response.success = true;
        
        return response;
    }
    
    Response post(const std::string& endpoint, const std::string& json_data) {
        Response response;
        CurlPtr curl(curl_easy_init());
        
        if (!curl) {
            response.success = false;
            return response;
        }
        
        std::string url = base_url + endpoint;
        std::string response_body;
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_data.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
        
        CURLcode res = curl_easy_perform(curl.get());
        
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            std::cerr << "POST request failed: " << curl_easy_strerror(res) << std::endl;
            response.success = false;
            return response;
        }
        
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = response_body;
        response.success = true;
        
        return response;
    }
    
    Response put(const std::string& endpoint, const std::string& json_data) {
        Response response;
        CurlPtr curl(curl_easy_init());
        
        if (!curl) {
            response.success = false;
            return response;
        }
        
        std::string url = base_url + endpoint;
        std::string response_body;
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_data.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
        
        CURLcode res = curl_easy_perform(curl.get());
        
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            std::cerr << "PUT request failed: " << curl_easy_strerror(res) << std::endl;
            response.success = false;
            return response;
        }
        
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = response_body;
        response.success = true;
        
        return response;
    }
    
    Response del(const std::string& endpoint) {
        Response response;
        CurlPtr curl(curl_easy_init());
        
        if (!curl) {
            response.success = false;
            return response;
        }
        
        std::string url = base_url + endpoint;
        std::string response_body;
        
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
        
        CURLcode res = curl_easy_perform(curl.get());
        
        if (res != CURLE_OK) {
            std::cerr << "DELETE request failed: " << curl_easy_strerror(res) << std::endl;
            response.success = false;
            return response;
        }
        
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = response_body;
        response.success = true;
        
        return response;
    }
    
    Response patch(const std::string& endpoint, const std::string& json_data) {
        Response response;
        CurlPtr curl(curl_easy_init());
        
        if (!curl) {
            response.success = false;
            return response;
        }
        
        std::string url = base_url + endpoint;
        std::string response_body;
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_data.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
        
        CURLcode res = curl_easy_perform(curl.get());
        
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            std::cerr << "PATCH request failed: " << curl_easy_strerror(res) << std::endl;
            response.success = false;
            return response;
        }
        
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = response_body;
        response.success = true;
        
        return response;
    }
};

int main() {
    RestClient client("https://jsonplaceholder.typicode.com");
    
    std::cout << "=== REST API Examples in C++ ===" << std::endl << std::endl;
    
    // GET request
    std::cout << "1. GET /users/1" << std::endl;
    auto get_response = client.get("/users/1");
    if (get_response.success) {
        std::cout << "Status: " << get_response.status_code << std::endl;
        std::cout << "Response: " << get_response.body << std::endl;
    }
    std::cout << std::endl;
    
    // POST request
    std::cout << "2. POST /posts" << std::endl;
    std::string post_data = R"({
        "title": "My C++ Post",
        "body": "This is a post created from C++",
        "userId": 1
    })";
    auto post_response = client.post("/posts", post_data);
    if (post_response.success) {
        std::cout << "Status: " << post_response.status_code << std::endl;
        std::cout << "Response: " << post_response.body << std::endl;
    }
    std::cout << std::endl;
    
    // PUT request
    std::cout << "3. PUT /posts/1" << std::endl;
    std::string put_data = R"({
        "id": 1,
        "title": "Updated from C++",
        "body": "This post was updated using PUT",
        "userId": 1
    })";
    auto put_response = client.put("/posts/1", put_data);
    if (put_response.success) {
        std::cout << "Status: " << put_response.status_code << std::endl;
        std::cout << "Response: " << put_response.body << std::endl;
    }
    std::cout << std::endl;
    
    // PATCH request
    std::cout << "4. PATCH /posts/1" << std::endl;
    std::string patch_data = R"({
        "title": "Partially Updated Title"
    })";
    auto patch_response = client.patch("/posts/1", patch_data);
    if (patch_response.success) {
        std::cout << "Status: " << patch_response.status_code << std::endl;
        std::cout << "Response: " << patch_response.body << std::endl;
    }
    std::cout << std::endl;
    
    // DELETE request
    std::cout << "5. DELETE /posts/1" << std::endl;
    auto delete_response = client.del("/posts/1");
    if (delete_response.success) {
        std::cout << "Status: " << delete_response.status_code << std::endl;
        std::cout << "Deletion successful!" << std::endl;
    }
    
    return 0;
}
```

**Compilation:**
```bash
g++ -std=c++14 -o rest_client_cpp rest_client.cpp -lcurl
```

## Example 3: Rust Implementation

This Rust example uses the `reqwest` crate for making HTTP requests. It demonstrates both blocking and async approaches.

**Cargo.toml:**
```toml
[package]
name = "rest_client_rust"
version = "0.1.0"
edition = "2021"

[dependencies]
reqwest = { version = "0.11", features = ["json", "blocking"] }
tokio = { version = "1", features = ["full"] }
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
```

**src/main.rs:**
```rust
use reqwest::blocking::Client;
use reqwest::header::{HeaderMap, HeaderValue, CONTENT_TYPE};
use serde::{Deserialize, Serialize};
use std::error::Error;

// Data structures for JSON serialization/deserialization
#[derive(Debug, Serialize, Deserialize)]
struct User {
    id: Option<u32>,
    name: String,
    username: String,
    email: String,
}

#[derive(Debug, Serialize, Deserialize)]
struct Post {
    #[serde(skip_serializing_if = "Option::is_none")]
    id: Option<u32>,
    title: String,
    body: String,
    #[serde(rename = "userId")]
    user_id: u32,
}

// REST Client structure
struct RestClient {
    base_url: String,
    client: Client,
}

impl RestClient {
    fn new(base_url: &str) -> Self {
        RestClient {
            base_url: base_url.to_string(),
            client: Client::new(),
        }
    }
    
    // GET request
    fn get(&self, endpoint: &str) -> Result<String, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        let response = self.client.get(&url).send()?;
        
        let status = response.status();
        let body = response.text()?;
        
        println!("Status: {}", status);
        Ok(body)
    }
    
    // GET request with JSON deserialization
    fn get_json<T: for<'de> Deserialize<'de>>(&self, endpoint: &str) -> Result<T, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        let response = self.client.get(&url).send()?;
        
        let status = response.status();
        println!("Status: {}", status);
        
        let data: T = response.json()?;
        Ok(data)
    }
    
    // POST request
    fn post<T: Serialize>(&self, endpoint: &str, data: &T) -> Result<String, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        
        let mut headers = HeaderMap::new();
        headers.insert(CONTENT_TYPE, HeaderValue::from_static("application/json"));
        
        let response = self.client
            .post(&url)
            .headers(headers)
            .json(data)
            .send()?;
        
        let status = response.status();
        let body = response.text()?;
        
        println!("Status: {}", status);
        Ok(body)
    }
    
    // POST request with JSON response
    fn post_json<T: Serialize, R: for<'de> Deserialize<'de>>(
        &self,
        endpoint: &str,
        data: &T,
    ) -> Result<R, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        
        let response = self.client
            .post(&url)
            .json(data)
            .send()?;
        
        let status = response.status();
        println!("Status: {}", status);
        
        let result: R = response.json()?;
        Ok(result)
    }
    
    // PUT request
    fn put<T: Serialize>(&self, endpoint: &str, data: &T) -> Result<String, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        
        let response = self.client
            .put(&url)
            .json(data)
            .send()?;
        
        let status = response.status();
        let body = response.text()?;
        
        println!("Status: {}", status);
        Ok(body)
    }
    
    // PATCH request
    fn patch<T: Serialize>(&self, endpoint: &str, data: &T) -> Result<String, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        
        let response = self.client
            .patch(&url)
            .json(data)
            .send()?;
        
        let status = response.status();
        let body = response.text()?;
        
        println!("Status: {}", status);
        Ok(body)
    }
    
    // DELETE request
    fn delete(&self, endpoint: &str) -> Result<(), Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        
        let response = self.client.delete(&url).send()?;
        
        let status = response.status();
        println!("Status: {}", status);
        
        if status.is_success() {
            println!("Resource deleted successfully");
        }
        
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    println!("=== REST API Examples in Rust ===\n");
    
    let client = RestClient::new("https://jsonplaceholder.typicode.com");
    
    // 1. GET request - Fetch a user
    println!("1. GET /users/1");
    let user: User = client.get_json("/users/1")?;
    println!("User: {:?}\n", user);
    
    // 2. GET request - Fetch all posts (limiting to first 5)
    println!("2. GET /posts?_limit=5");
    let posts_json = client.get("/posts?_limit=5")?;
    println!("Posts: {}\n", posts_json);
    
    // 3. POST request - Create a new post
    println!("3. POST /posts");
    let new_post = Post {
        id: None,
        title: "My Rust Post".to_string(),
        body: "This is a post created from Rust".to_string(),
        user_id: 1,
    };
    let created_post: Post = client.post_json("/posts", &new_post)?;
    println!("Created Post: {:?}\n", created_post);
    
    // 4. PUT request - Update a post
    println!("4. PUT /posts/1");
    let updated_post = Post {
        id: Some(1),
        title: "Updated from Rust".to_string(),
        body: "This post was updated using PUT from Rust".to_string(),
        user_id: 1,
    };
    let put_response = client.put("/posts/1", &updated_post)?;
    println!("Updated Post: {}\n", put_response);
    
    // 5. PATCH request - Partially update a post
    println!("5. PATCH /posts/1");
    let patch_data = serde_json::json!({
        "title": "Partially Updated Title from Rust"
    });
    let patch_response = client.patch("/posts/1", &patch_data)?;
    println!("Patched Post: {}\n", patch_response);
    
    // 6. DELETE request - Delete a post
    println!("6. DELETE /posts/1");
    client.delete("/posts/1")?;
    println!();
    
    Ok(())
}
```

**Async version (src/async_main.rs):**
```rust
use reqwest::Client;
use serde::{Deserialize, Serialize};
use std::error::Error;

#[derive(Debug, Serialize, Deserialize)]
struct Post {
    #[serde(skip_serializing_if = "Option::is_none")]
    id: Option<u32>,
    title: String,
    body: String,
    #[serde(rename = "userId")]
    user_id: u32,
}

struct AsyncRestClient {
    base_url: String,
    client: Client,
}

impl AsyncRestClient {
    fn new(base_url: &str) -> Self {
        AsyncRestClient {
            base_url: base_url.to_string(),
            client: Client::new(),
        }
    }
    
    async fn get<T: for<'de> Deserialize<'de>>(&self, endpoint: &str) -> Result<T, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        let response = self.client.get(&url).send().await?;
        
        let status = response.status();
        println!("Status: {}", status);
        
        let data: T = response.json().await?;
        Ok(data)
    }
    
    async fn post<T: Serialize, R: for<'de> Deserialize<'de>>(
        &self,
        endpoint: &str,
        data: &T,
    ) -> Result<R, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        let response = self.client.post(&url).json(data).send().await?;
        
        let status = response.status();
        println!("Status: {}", status);
        
        let result: R = response.json().await?;
        Ok(result)
    }
    
    async fn put<T: Serialize>(&self, endpoint: &str, data: &T) -> Result<String, Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        let response = self.client.put(&url).json(data).send().await?;
        
        let status = response.status();
        println!("Status: {}", status);
        
        let body = response.text().await?;
        Ok(body)
    }
    
    async fn delete(&self, endpoint: &str) -> Result<(), Box<dyn Error>> {
        let url = format!("{}{}", self.base_url, endpoint);
        let response = self.client.delete(&url).send().await?;
        
        let status = response.status();
        println!("Status: {}", status);
        
        if status.is_success() {
            println!("Resource deleted successfully");
        }
        
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    println!("=== Async REST API Examples in Rust ===\n");
    
    let client = AsyncRestClient::new("https://jsonplaceholder.typicode.com");
    
    // Concurrent requests using tokio::join!
    println!("Making concurrent requests...\n");
    
    let (post1, post2, post3) = tokio::join!(
        client.get::<Post>("/posts/1"),
        client.get::<Post>("/posts/2"),
        client.get::<Post>("/posts/3")
    );
    
    println!("Post 1: {:?}", post1?);
    println!("Post 2: {:?}", post2?);
    println!("Post 3: {:?}\n", post3?);
    
    // Sequential async operations
    let new_post = Post {
        id: None,
        title: "Async Rust Post".to_string(),
        body: "Created asynchronously".to_string(),
        user_id: 1,
    };
    
    let created: Post = client.post("/posts", &new_post).await?;
    println!("Created async post: {:?}\n", created);
    
    Ok(())
}
```

**Build and run:**
```bash
cargo build --release
cargo run
```

## Key Differences Between Implementations

**C Implementation:**
- Manual memory management with malloc/free
- Explicit error handling with return codes
- Lower-level control but more verbose
- Direct use of libcurl C API
- Requires careful buffer management

**C++ Implementation:**
- RAII for automatic resource management (unique_ptr for CURL handle)
- Exception handling capabilities
- More expressive with classes and objects
- Smart pointers prevent memory leaks
- Raw string literals (R"(...)") for JSON

**Rust Implementation:**
- Memory safety guaranteed at compile time
- Excellent error handling with Result type
- Zero-cost abstractions
- Built-in JSON serialization/deserialization with serde
- Async/await for concurrent operations
- No garbage collection but automatic memory management
- Type safety for HTTP responses

## Best Practices for RESTful APIs

**Versioning:** Include API version in URL (`/api/v1/users`) or headers.

**Authentication:** Use industry-standard methods like OAuth 2.0, JWT tokens, or API keys.

**Rate Limiting:** Implement rate limiting to prevent abuse. Return appropriate headers like `X-RateLimit-Remaining`.

**Pagination:** For large collections, implement pagination using query parameters like `?page=1&limit=20` or cursor-based pagination.

**Error Handling:** Return consistent error responses with meaningful messages and appropriate HTTP status codes.

**Documentation:** Provide comprehensive API documentation (OpenAPI/Swagger).

**HTTPS Only:** Always use HTTPS in production to encrypt data in transit.

**Idempotency:** Ensure PUT, DELETE, and GET requests are idempotent. Use idempotency keys for POST requests when necessary.

**CORS:** Properly configure Cross-Origin Resource Sharing for browser-based clients.

**Compression:** Enable gzip compression to reduce bandwidth usage.

This comprehensive overview covers REST principles and practical implementations across three different programming languages, demonstrating how REST APIs can be consumed in various development environments.